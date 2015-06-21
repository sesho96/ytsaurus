#include "stdafx.h"
#include "snapshot_discovery.h"
#include "private.h"
#include "config.h"

#include <core/concurrency/thread_affinity.h>

#include <ytlib/election/cell_manager.h>

#include <server/hydra/snapshot_service_proxy.h>

namespace NYT {
namespace NHydra {

using namespace NElection;
using namespace NConcurrency;

////////////////////////////////////////////////////////////////////////////////

class TSnapshotDiscovery
    : public TRefCounted
{
public:
    TSnapshotDiscovery(
        TDistributedHydraManagerConfigPtr config,
        TCellManagerPtr cellManager)
        : Config_(config)
        , CellManager_(cellManager)
    {
        YCHECK(Config_);
        YCHECK(CellManager_);

        Logger.AddTag("CellId: %v", CellManager_->GetCellId());
    }

    TFuture<TRemoteSnapshotParams> Run(int maxSnapshotId, bool exactId)
    {
        if (exactId) {
            LOG_INFO("Looking for snapshot %v", maxSnapshotId);
        } else {
            LOG_INFO("Looking for the latest snapshot up to %v", maxSnapshotId);
        }

        std::vector<TFuture<void>> asyncResults;
        for (auto peerId = 0; peerId < CellManager_->GetPeerCount(); ++peerId) {
            auto channel = CellManager_->GetPeerChannel(peerId);
            if (!channel)
                continue;

            LOG_INFO("Requesting snapshot info from peer %v", peerId);

            TSnapshotServiceProxy proxy(channel);
            proxy.SetDefaultTimeout(Config_->ControlRpcTimeout);

            auto req = proxy.LookupSnapshot();
            req->set_max_snapshot_id(maxSnapshotId);
            req->set_exact_id(exactId);
            asyncResults.push_back(req->Invoke().Apply(
                BIND(&TSnapshotDiscovery::OnResponse, MakeStrong(this), peerId)));
        }
        LOG_INFO("Snapshot lookup requests sent");

        Combine(asyncResults).Subscribe(
            BIND(&TSnapshotDiscovery::OnComplete, MakeStrong(this)));

        return Promise_;
    }

private:
    const TDistributedHydraManagerConfigPtr Config_;
    const NElection::TCellManagerPtr CellManager_;

    TPromise<TRemoteSnapshotParams> Promise_ = NewPromise<TRemoteSnapshotParams>();

    TSpinLock SpinLock_;
    TRemoteSnapshotParams Params_;

    NLogging::TLogger Logger = HydraLogger;


    void OnResponse(
        TPeerId peerId,
        const TSnapshotServiceProxy::TErrorOrRspLookupSnapshotPtr& rspOrError)
    {
        VERIFY_THREAD_AFFINITY_ANY();

        if (!rspOrError.IsOK()) {
            LOG_WARNING(rspOrError, "Error looking up snapshots at peer %v",
                peerId);
            return;
        }

        const auto& rsp = rspOrError.Value();
        LOG_INFO("Found snapshot %v found on peer %v",
            rsp->snapshot_id(),
            peerId);

        {
            TGuard<TSpinLock> guard(SpinLock_);
            if (rsp->snapshot_id() > Params_.SnapshotId) {
                Params_.PeerId = peerId;
                Params_.SnapshotId = rsp->snapshot_id();
                Params_.CompressedLength = rsp->compressed_length();
                Params_.UncompressedLength = rsp->uncompressed_length();
                Params_.Checksum = rsp->checksum();
                Params_.Meta = rsp->meta();
            }
        }
    }

    void OnComplete(const TError&)
    {
        VERIFY_THREAD_AFFINITY_ANY();

        if (Params_.SnapshotId == InvalidSegmentId) {
            LOG_INFO("Snapshot lookup failed, no suitable snapshot found");
        } else {
            LOG_INFO("Snapshot lookup succeeded (PeerId: %v, SnapshotId: %v)",
                Params_.PeerId,
                Params_.SnapshotId);
        }

        Promise_.Set(Params_);
    }

};

TFuture<TRemoteSnapshotParams> DiscoverLatestSnapshot(
    TDistributedHydraManagerConfigPtr config,
    TCellManagerPtr cellManager,
    int maxSnapshotId)
{
    auto discovery = New<TSnapshotDiscovery>(config, cellManager);
    return discovery->Run(maxSnapshotId, false);
}

TFuture<TRemoteSnapshotParams> DiscoverSnapshot(
    TDistributedHydraManagerConfigPtr config,
    TCellManagerPtr cellManager,
    int snapshotId)
{
    auto discovery = New<TSnapshotDiscovery>(config, cellManager);
    return discovery->Run(snapshotId, true);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NHydra
} // namespace NYT
