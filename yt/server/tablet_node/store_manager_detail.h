#pragma once

#include "store_manager.h"

#include <yt/server/hydra/public.h>

#include <yt/ytlib/api/public.h>

#include <yt/core/logging/log.h>

namespace NYT {
namespace NTabletNode {

////////////////////////////////////////////////////////////////////////////////

class TStoreManagerBase
    : public virtual IStoreManager
{
public:
    TStoreManagerBase(
        TTabletManagerConfigPtr config,
        TTablet* tablet,
        ITabletContext* tabletContext,
        NHydra::IHydraManagerPtr hydraManager,
        TInMemoryManagerPtr inMemoryManager,
        NApi::IClientPtr client);

    virtual TTablet* GetTablet() const override;

    virtual bool HasActiveLocks() const override;
    virtual bool HasUnflushedStores() const override;

    virtual void StartEpoch(TTabletSlotPtr slot) override;
    virtual void StopEpoch() override;

    virtual bool IsRotationScheduled() const override;
    virtual void ScheduleRotation() override;

    virtual void AddStore(IStorePtr store, bool onMount) override;

    virtual void RemoveStore(IStorePtr store) override;
    virtual void BackoffStoreRemoval(IStorePtr store) override;

    virtual bool IsStoreFlushable(IStorePtr store) const override;
    virtual TStoreFlushCallback BeginStoreFlush(
        IDynamicStorePtr store,
        TTabletSnapshotPtr tabletSnapshot) override;
    virtual void EndStoreFlush(IDynamicStorePtr store) override;
    virtual void BackoffStoreFlush(IDynamicStorePtr store) override;

    virtual void BeginStoreCompaction(IChunkStorePtr store) override;
    virtual void EndStoreCompaction(IChunkStorePtr store) override;
    virtual void BackoffStoreCompaction(IChunkStorePtr store) override;

    virtual IChunkStorePtr PeekStoreForPreload() override;
    virtual void BeginStorePreload(
        IChunkStorePtr store,
        TCallback<TFuture<void>()> callbackFuture) override;
    virtual void EndStorePreload(IChunkStorePtr store) override;
    virtual void BackoffStorePreload(IChunkStorePtr store) override;

    virtual void Mount(
        const std::vector<NTabletNode::NProto::TAddStoreDescriptor>& storeDescriptors) override;
    virtual void Remount(
        TTableMountConfigPtr mountConfig,
        TTabletWriterOptionsPtr writerOptions) override;

    virtual void Rotate(bool createNewStore) override;

    virtual bool IsStoreLocked(IStorePtr store) const override;
    virtual std::vector<IStorePtr> GetLockedStores() const override;

    virtual bool IsOverflowRotationNeeded() const override;
    virtual bool IsPeriodicRotationNeeded() const override;
    virtual bool IsRotationPossible() const override;
    virtual bool IsForcedRotationPossible() const override;

    virtual ISortedStoreManagerPtr AsSorted() override;
    virtual IOrderedStoreManagerPtr AsOrdered() override;

protected:
    const TTabletManagerConfigPtr Config_;
    TTablet* const Tablet_;
    ITabletContext* const TabletContext_;
    const NHydra::IHydraManagerPtr HydraManager_;
    const TInMemoryManagerPtr InMemoryManager_;
    const NApi::IClientPtr Client_;

    bool RotationScheduled_ = false;
    TInstant LastRotated_;

    yhash_set<IStorePtr> LockedStores_;

    NLogging::TLogger Logger;

    virtual IDynamicStore* GetActiveStore() const = 0;
    virtual void ResetActiveStore() = 0;
    virtual void OnActiveStoreRotated() = 0;

    virtual TStoreFlushCallback MakeStoreFlushCallback(
        IDynamicStorePtr store,
        TTabletSnapshotPtr tabletSnapshot) = 0;

    virtual void CreateActiveStore() = 0;

    void CheckForUnlockedStore(IDynamicStore* store);

    void UpdateInMemoryMode();

    void ScheduleStorePreload(IChunkStorePtr store);
    bool TryPreloadStoreFromInterceptedData(
        IChunkStorePtr store,
        TInMemoryChunkDataPtr chunkData);

    bool IsRecovery() const;

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletNode
} // namespace NYT
