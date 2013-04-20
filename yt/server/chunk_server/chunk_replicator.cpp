#include "stdafx.h"
#include "chunk_replicator.h"
#include "chunk_placement.h"
#include "job.h"
#include "chunk.h"
#include "chunk_list.h"
#include "chunk_tree_traversing.h"
#include "private.h"

#include <ytlib/misc/foreach.h>
#include <ytlib/misc/serialize.h>
#include <ytlib/misc/string.h>
#include <ytlib/misc/small_vector.h>
#include <ytlib/misc/protobuf_helpers.h>

#include <ytlib/node_tracker_client/node_directory.h>
#include <ytlib/node_tracker_client/helpers.h>

#include <ytlib/erasure/codec.h>

#include <ytlib/profiling/profiler.h>
#include <ytlib/profiling/timing.h>

#include <server/cell_master/bootstrap.h>
#include <server/cell_master/config.h>
#include <server/cell_master/meta_state_facade.h>

#include <server/chunk_server/chunk_manager.h>
#include <server/chunk_server/node_directory_builder.h>

#include <server/node_tracker_server/node_tracker.h>
#include <server/node_tracker_server/node.h>

#include <server/cypress_server/node.h>

namespace NYT {
namespace NChunkServer {

using namespace NCellMaster;
using namespace NObjectClient;
using namespace NProfiling;
using namespace NChunkClient;
using namespace NNodeTrackerClient;

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger = ChunkServerLogger;
static NProfiling::TProfiler& Profiler = ChunkServerProfiler;

////////////////////////////////////////////////////////////////////////////////

TChunkReplicator::TChunkReplicator(
    TChunkManagerConfigPtr config,
    TBootstrap* bootstrap,
    TChunkPlacementPtr chunkPlacement)
    : Config(config)
    , Bootstrap(bootstrap)
    , ChunkPlacement(chunkPlacement)
    , ChunkRefreshDelay(DurationToCpuDuration(config->ChunkRefreshDelay))
{
    YCHECK(config);
    YCHECK(bootstrap);
    YCHECK(chunkPlacement);
}

void TChunkReplicator::Initialize()
{
    RefreshInvoker = New<TPeriodicInvoker>(
        Bootstrap->GetMetaStateFacade()->GetEpochInvoker(EStateThreadQueue::ChunkMaintenance),
        BIND(&TChunkReplicator::OnRefresh, MakeWeak(this)),
        Config->ChunkRefreshPeriod);
    RefreshInvoker->Start();

    RFUpdateInvoker = New<TPeriodicInvoker>(
        Bootstrap->GetMetaStateFacade()->GetEpochInvoker(EStateThreadQueue::ChunkMaintenance),
        BIND(&TChunkReplicator::OnRFUpdate, MakeWeak(this)),
        Config->ChunkRFUpdatePeriod,
        EPeriodicInvokerMode::Manual);
    RFUpdateInvoker->Start();

    auto nodeTracker = Bootstrap->GetNodeTracker();
    FOREACH (auto* node, nodeTracker->GetNodes()) {
        OnNodeRegistered(node);
    }

    auto chunkManager = Bootstrap->GetChunkManager();
    FOREACH (auto* chunk, chunkManager->GetChunks()) {
        ScheduleChunkRefresh(chunk);
        ScheduleRFUpdate(chunk);
    }
}

TJobPtr TChunkReplicator::FindJob(const TJobId& id)
{
    auto it = JobMap.find(id);
    return it == JobMap.end() ? nullptr : it->second;
}

TJobListPtr TChunkReplicator::FindJobList(const TChunkId& id)
{
    auto it = JobListMap.find(id);
    return it == JobListMap.end() ? nullptr : it->second;
}

void TChunkReplicator::ScheduleJobs(
    TNode* node,
    const std::vector<TJobPtr>& runningJobs,
    std::vector<TJobPtr>* jobsToStart,
    std::vector<TJobPtr>* jobsToAbort,
    std::vector<TJobPtr>* jobsToRemove)
{
    ProcessExistingJobs(
        node,
        runningJobs,
        jobsToAbort,
        jobsToRemove);

    if (IsEnabled()) {
        ScheduleNewJobs(node, jobsToStart);
    }

    FOREACH (auto job, *jobsToStart) {
        RegisterJob(job);
    }

    FOREACH (auto job, *jobsToRemove) {
        UnregisterJob(job);
    }
}

void TChunkReplicator::OnNodeRegistered(TNode* node)
{
    node->ChunksToRemove().clear();

    FOREACH (auto& chunksToReplicate, node->ChunksToReplicate()) {
        chunksToReplicate.clear();
    }

    FOREACH (auto replica, node->StoredReplicas()) {
        ScheduleChunkRefresh(replica.GetPtr());
    }
}

void TChunkReplicator::OnNodeUnregistered(TNode* node)
{
    // Make a copy, UnregisterJob will modify the collection.
    auto jobs = node->Jobs();
    FOREACH (auto job, node->Jobs()) {
        UnregisterJob(job);
    }
}

void TChunkReplicator::ResetChunk(TChunk* chunk)
{
    ResetChunkStatus(chunk);

    const auto& chunkId = chunk->GetId();
    auto it = JobListMap.find(chunkId);
    if (it != JobListMap.end()) {
        auto jobList = it->second;
        FOREACH (auto job, jobList->Jobs()) {
            LOG_INFO("Job unregistered (JobId: %s, Address: %s)",
                ~ToString(job->GetJobId()),
                ~job->GetNode()->GetAddress());
            YCHECK(JobMap.erase(job->GetJobId()) == 1);
        }
        JobListMap.erase(it);
    }
}

void TChunkReplicator::ScheduleUnknownChunkRemoval(TNode* node, const TChunkId& chunkId)
{
    node->ChunksToRemove().insert(chunkId);
}

void TChunkReplicator::ScheduleChunkRemoval(TNode* node, TChunkPtrWithIndex chunkWithIndex)
{
    auto chunkId = EncodeChunkId(chunkWithIndex);
    node->ChunksToRemove().insert(chunkId);
}

void TChunkReplicator::ProcessExistingJobs(
    TNode* node,
    const std::vector<TJobPtr>& currentJobs,
    std::vector<TJobPtr>* jobsToAbort,
    std::vector<TJobPtr>* jobsToRemove)
{
    const auto& address = node->GetAddress();

    auto chunkManager = Bootstrap->GetChunkManager();
    FOREACH (const auto& job, currentJobs) {
        const auto& jobId = job->GetJobId();
        auto* chunk = chunkManager->FindChunk(job->GetChunkId());

        switch (job->GetState()) {
            case EJobState::Running:
                if (TInstant::Now() - job->GetStartTime() > Config->ChunkReplicator->JobTimeout) {
                    jobsToAbort->push_back(job);
                    LOG_WARNING("Job timed out (JobId: %s, Address: %s, Duration: %s)",
                        ~ToString(jobId),
                        ~address,
                        ~ToString(TInstant::Now() - job->GetStartTime()));
                } else {
                    LOG_INFO("Job is running (JobId: %s, Address: %s)",
                        ~ToString(jobId),
                        ~address);
                }
                break;

            case EJobState::Completed:
            case EJobState::Failed:
            case EJobState::Aborted: {
                jobsToRemove->push_back(job);

                if (chunk) {
                    ScheduleChunkRefresh(chunk);
                }

                switch (job->GetState()) {
                    case EJobState::Completed:
                        LOG_INFO("Job completed (JobId: %s, Address: %s)",
                            ~ToString(jobId),
                            ~address);
                        break;

                    case EJobState::Failed:
                        LOG_WARNING(job->Error(), "Job failed (JobId: %s, Address: %s)",
                            ~ToString(jobId),
                            ~address);
                        break;

                    case EJobState::Aborted:
                        LOG_WARNING(job->Error(), "Job aborted (JobId: %s, Address: %s)",
                            ~ToString(jobId),
                            ~address);
                        break;

                    default:
                        YUNREACHABLE();
                }
                break;
            }


            default:
                YUNREACHABLE();
        }
    }

    // Check for missing jobs
    yhash_set<TJobPtr> currentJobSet(currentJobs.begin(), currentJobs.end());
    std::vector<TJobPtr> missingJobs;
    FOREACH (const auto& job, node->Jobs()) {
        if (currentJobSet.find(job) == currentJobSet.end()) {
            missingJobs.push_back(job);
            LOG_WARNING("Job is missing (JobId: %s, Address: %s)",
                ~ToString(job->GetJobId()),
                ~address);
        }
    }
    FOREACH (const auto& job, missingJobs) {
        UnregisterJob(job);
    }
}

TChunkReplicator::EScheduleFlags TChunkReplicator::ScheduleReplicationJob(
    TNode* sourceNode,
    TChunk* chunk,
    TJobPtr* job)
{
    const auto& chunkId = chunk->GetId();
    auto chunkManager = Bootstrap->GetChunkManager();

    if (!IsObjectAlive(chunk)) {
        return EScheduleFlags::Purged;
    }

    if (chunk->GetRefreshScheduled()) {
        return EScheduleFlags::Purged;
    }

    if (HasRunningJobs(chunkId)) {
        return EScheduleFlags::Purged;
    }

    int replicaCount = static_cast<int>(chunk->StoredReplicas().size());
    int replicationFactor = chunk->GetReplicationFactor();
    int replicasNeeded = replicationFactor - replicaCount;
    if (replicasNeeded <= 0) {
        return EScheduleFlags::Purged;
    }

    auto targets = ChunkPlacement->GetReplicationTargets(chunk, replicasNeeded);
    if (targets.empty()) {
        return EScheduleFlags::None;
    }

    std::vector<Stroka> targetAddresses;
    FOREACH (auto* target, targets) {
        ChunkPlacement->OnSessionHinted(target);
        targetAddresses.push_back(target->GetAddress());
    }

    *job = TJob::CreateReplicate(chunkId, sourceNode, targetAddresses);

    LOG_INFO("Replication job scheduled (JobId: %s, Address: %s, ChunkId: %s, TargetAddresses: [%s])",
        ~ToString((*job)->GetJobId()),
        ~sourceNode->GetAddress(),
        ~ToString(chunkId),
        ~JoinToString(targetAddresses));

    return
        targets.size() == replicasNeeded
        ? EScheduleFlags(EScheduleFlags::Purged | EScheduleFlags::Scheduled)
        : EScheduleFlags(EScheduleFlags::Scheduled);
}

TChunkReplicator::EScheduleFlags TChunkReplicator::ScheduleBalancingJob(
    TNode* sourceNode,
    TChunkPtrWithIndex chunkWithIndex,
    double maxFillCoeff,
    TJobPtr* job)
{
    auto* chunk = chunkWithIndex.GetPtr();
    const auto& chunkId = chunk->GetId();

    if (chunk->GetRefreshScheduled()) {
        return EScheduleFlags::Purged;
    }

    auto* targetNode = ChunkPlacement->GetBalancingTarget(chunkWithIndex, maxFillCoeff);
    if (!targetNode) {
        LOG_DEBUG("No suitable target nodes for balancing (ChunkId: %s)",
            ~ToString(chunkWithIndex));
        return EScheduleFlags::None;
    }

    ChunkPlacement->OnSessionHinted(targetNode);

    *job = TJob::CreateReplicate(chunkId, sourceNode, targetNode->GetAddress());

    LOG_INFO("Balancing job scheduled (JobId: %s, Address: %s, ChunkId: %s, TargetAddress: %s)",
        ~ToString((*job)->GetJobId()),
        ~sourceNode->GetAddress(),
        ~ToString(chunkId),
        ~targetNode->GetAddress());

    return EScheduleFlags(EScheduleFlags::Purged | EScheduleFlags::Scheduled);
}

TChunkReplicator::EScheduleFlags TChunkReplicator::ScheduleRemovalJob(
    TNode* node,
    const TChunkId& chunkId,
    TJobPtr* job)
{
    auto chunkManager = Bootstrap->GetChunkManager();

    auto* chunk = chunkManager->FindChunk(chunkId);
    if (chunk && chunk->GetRefreshScheduled()) {
        return EScheduleFlags::Purged;
    }

    if (HasRunningJobs(chunkId)) {
        return EScheduleFlags::Purged;
    }

    *job = TJob::CreateRemove(chunkId, node);

    LOG_INFO("Removal job scheduled (JobId: %s, Address: %s, ChunkId: %s)",
        ~ToString((*job)->GetJobId()),
        ~node->GetAddress(),
        ~ToString(chunkId));

    return EScheduleFlags(EScheduleFlags::Purged | EScheduleFlags::Scheduled);
}

TChunkReplicator::EScheduleFlags TChunkReplicator::ScheduleRepairJob(
    TNode* node,
    TChunk* chunk,
    TJobPtr* job)
{
    const auto& chunkId = chunk->GetId();

    if (!IsObjectAlive(chunk)) {
        return EScheduleFlags::Purged;
    }

    if (chunk->GetRefreshScheduled()) {
        return EScheduleFlags::Purged;
    }

    if (HasRunningJobs(chunkId)) {
        return EScheduleFlags::Purged;
    }

    auto codecId = chunk->GetErasureCodec();
    auto* codec = NErasure::GetCodec(codecId);
    
    auto totalBlockCount = codec->GetTotalBlockCount();

    NErasure::TBlockIndexSet replicaIndexSet((1 << totalBlockCount) - 1);
    int erasedIndexCount = totalBlockCount;
    FOREACH (auto replica, chunk->StoredReplicas()) {
        int index = replica.GetIndex();
        if (!replicaIndexSet[index]) {
            replicaIndexSet.set(index);
            --erasedIndexCount;
        }
    }
   
    auto targets = ChunkPlacement->GetReplicationTargets(chunk, erasedIndexCount);
    if (targets.size() != erasedIndexCount) {
        return EScheduleFlags::None;
    }

    std::vector<Stroka> targetAddresses;
    FOREACH (auto* target, targets) {
        ChunkPlacement->OnSessionHinted(target);
        targetAddresses.push_back(target->GetAddress());
    }

    *job = TJob::CreateRepair(chunkId, node, targetAddresses);

    LOG_INFO("Repair job scheduled (JobId: %s, Address: %s, ChunkId: %s, TargetAddresses: [%s])",
        ~ToString((*job)->GetJobId()),
        ~node->GetAddress(),
        ~ToString(chunkId),
        ~JoinToString(targetAddresses));

    return EScheduleFlags(EScheduleFlags::Purged | EScheduleFlags::Scheduled);
}

void TChunkReplicator::ScheduleNewJobs(
    TNode* node,
    std::vector<TJobPtr>* jobsToStart)
{
    auto registerJob = [&] (TJobPtr job) {
        jobsToStart->push_back(job);
        node->ResourceUsage() += job->ResourceLimits();
    };

    // Schedule replication jobs.
    FOREACH (auto& chunksToReplicate, node->ChunksToReplicate()) {
        auto it = chunksToReplicate.begin();
        while (it != chunksToReplicate.end()) {
            if (node->ResourceUsage().replication_slots() >= node->ResourceLimits().replication_slots())
                break;

            auto jt = it++;
            const auto& chunkId = *jt;

            TJobPtr job;
            auto flags = ScheduleReplicationJob(node, chunkId, &job);

            if (flags & EScheduleFlags::Scheduled) {
                registerJob(job);
            }
            if (flags & EScheduleFlags::Purged) {
                chunksToReplicate.erase(jt);
            }
        }
    }

    // Schedule removal jobs.
    {
        auto& chunksToRemove = node->ChunksToRemove();
        auto it = chunksToRemove.begin();
        while (it != chunksToRemove.end()) {
            if (node->ResourceUsage().removal_slots() >= node->ResourceLimits().removal_slots())
                break;

            auto jt = it++;
            const auto& chunkId = *jt;

            TJobPtr job;
            auto flags = ScheduleRemovalJob(node, chunkId, &job);

            if (flags & EScheduleFlags::Scheduled) {
                registerJob(job);
            }
            if (flags & EScheduleFlags::Purged) {
                chunksToRemove.erase(jt);
            }
        }
    }

    // Schedule balancing jobs.
    double sourceFillCoeff = ChunkPlacement->GetFillCoeff(node);
    double targetFillCoeff = sourceFillCoeff - Config->ChunkReplicator->MinBalancingFillCoeffDiff;
    if (node->ResourceUsage().replication_slots() < node->ResourceLimits().replication_slots() &&
        sourceFillCoeff > Config->ChunkReplicator->MinBalancingFillCoeff &&
        ChunkPlacement->HasBalancingTargets(targetFillCoeff))
    {
        int maxJobs = std::max(0, node->ResourceLimits().replication_slots() - node->ResourceUsage().replication_slots());
        auto chunksToBalance = ChunkPlacement->GetBalancingChunks(node, maxJobs);
        FOREACH (auto chunkWithIndex, chunksToBalance) {
            if (node->ResourceUsage().replication_slots() >= node->ResourceLimits().replication_slots())
                break;

            TJobPtr job;
            auto flags = ScheduleBalancingJob(node, chunkWithIndex, targetFillCoeff, &job);

            if (flags & EScheduleFlags::Scheduled) {
                registerJob(job);
            }
        }
    }

    // Schedule repair jobs.
    {
        auto it = ChunksToRepair.begin();
        while (it != ChunksToRepair.end()) {
            if (node->ResourceUsage().repair_slots() >= node->ResourceLimits().repair_slots())
                break;

            auto jt = it++;

            TJobPtr job;
            auto flags = ScheduleRepairJob(node, *it, &job);
            if (flags & EScheduleFlags::Scheduled) {
                registerJob(job);
            }
            if (flags & EScheduleFlags::Purged) {
                ChunksToRepair.erase(jt);
            }
        }
    }

}

void TChunkReplicator::Refresh(TChunk* chunk)
{
    const auto& chunkId = chunk->GetId();

    if (!chunk->IsConfirmed()) {
        return;
    }

    if (HasRunningJobs(chunkId)) {
        return;
    }

    ResetChunkStatus(chunk);
    
    if (chunk->IsErasure()) {
        ComputeErasureChunkStatus(chunk);
    } else {
        ComputeRegularChunkStatus(chunk);
    }
}

void TChunkReplicator::ResetChunkStatus(TChunk* chunk)
{
    FOREACH (auto nodeWithIndex, chunk->StoredReplicas()) {
        auto* node = nodeWithIndex.GetPtr();
        TChunkPtrWithIndex chunkWithIndex(chunk, nodeWithIndex.GetIndex());
        FOREACH (auto& chunksToReplicate, node->ChunksToReplicate()) {
            chunksToReplicate.erase(chunk);
        }
        auto chunkId = EncodeChunkId(chunkWithIndex);
        node->ChunksToRemove().erase(chunkId);
    }

    LostChunks_.erase(chunk);
    LostVitalChunks_.erase(chunk);
    
    if (chunk->IsErasure()) {
        DataMissingChunks_.erase(chunk);
        ParityMissingChunks_.erase(chunk);  
        ChunksToRepair.erase(chunk);
    } else {
        OverreplicatedChunks_.erase(chunk);
        UnderreplicatedChunks_.erase(chunk);  
    }
}

void TChunkReplicator::ComputeRegularChunkStatus(TChunk* chunk)
{
    const auto& chunkId = chunk->GetId();
    int replicaCount = static_cast<int>(chunk->StoredReplicas().size());
    int replicationFactor = chunk->GetReplicationFactor();
    if (replicaCount == 0) {
        // Lost!
        YCHECK(LostChunks_.insert(chunk).second);
        if (chunk->GetVital()) {
            YCHECK(LostVitalChunks_.insert(chunk).second);
        }
    } else if (replicaCount > replicationFactor) {
        // Overreplicated chunk.
        YCHECK(OverreplicatedChunks_.insert(chunk).second);

        int redundantCount = replicaCount - replicationFactor;
        auto nodes = ChunkPlacement->GetRemovalTargets(TChunkPtrWithIndex(chunk), redundantCount);

        TSmallVector<Stroka, TypicalReplicationFactor> addresses;
        FOREACH (auto* node, nodes) {
            YCHECK(node->ChunksToRemove().insert(chunkId).second);
            addresses.push_back(node->GetAddress());
        }
    } else if (replicaCount < replicationFactor) {
        // Underreplicated chunk.
        YCHECK(UnderreplicatedChunks_.insert(chunk).second);

        auto* node = ChunkPlacement->GetReplicationSource(chunk);

        int priority = std::min(replicaCount, ReplicationPriorityCount) - 1;
        YCHECK(node->ChunksToReplicate()[priority].insert(chunk).second);
    }
}

void TChunkReplicator::ComputeErasureChunkStatus(TChunk* chunk)
{
    const auto& chunkId = chunk->GetId();

    // Check data and parity parts.
    NErasure::TBlockIndexSet replicaIndexSet(0);
    int replicaCount[NErasure::MaxTotalBlockCount] = {};
    TSmallVector<int, NErasure::MaxTotalBlockCount> overreplicatedIndexes;
    FOREACH (auto replica, chunk->StoredReplicas()) {
        int index = replica.GetIndex();
        if (++replicaCount[index] > 1) {
            overreplicatedIndexes.push_back(index);
        }
        replicaIndexSet.set(index);
    }

    auto* codec = NErasure::GetCodec(chunk->GetErasureCodec());
    int dataBlockCount = codec->GetDataBlockCount();
    int partityBlockCount = codec->GetParityBlockCount();

    auto dataIndexSet = NErasure::TBlockIndexSet((1 << dataBlockCount) - 1);
    auto parityIndexSet = NErasure::TBlockIndexSet(((1 << partityBlockCount) - 1) << dataBlockCount);

    if ((replicaIndexSet & dataIndexSet) != dataIndexSet) {
        // Data is missing.
        YCHECK(DataMissingChunks_.insert(chunk).second);
    }

    if ((replicaIndexSet & parityIndexSet) != parityIndexSet) {
       // Parity is missing.
        YCHECK(ParityMissingChunks_.insert(chunk).second);
    }

    if (replicaIndexSet != (dataIndexSet | parityIndexSet)) {
        // Something is damaged.
        if (codec->CanRepair(replicaIndexSet)) {
            // Will repair it.
            YCHECK(ChunksToRepair.insert(chunk).second);
        } else {
            // Lost!
            YCHECK(LostChunks_.insert(chunk).second);
            if (chunk->GetVital()) {
                YCHECK(LostVitalChunks_.insert(chunk).second);
            }
        }
    }

    // Check for overreplicated parts.
    FOREACH (int index, overreplicatedIndexes) {
        TChunkPtrWithIndex chunkWithIndex(chunk, index);
        auto encodedChunkId = EncodeChunkId(chunkWithIndex);
        int redundantCount = replicaCount[index] - 1;
        auto nodes = ChunkPlacement->GetRemovalTargets(chunkWithIndex, redundantCount);

        TSmallVector<Stroka, TypicalReplicationFactor> addresses;
        FOREACH (auto* node, nodes) {
            YCHECK(node->ChunksToRemove().insert(encodedChunkId).second);
            addresses.push_back(node->GetAddress());
        }
    }
}

bool TChunkReplicator::HasRunningJobs(const TChunkId& chunkId)
{
    auto chunkManager = Bootstrap->GetChunkManager();
    auto jobList = chunkManager->FindJobList(chunkId);
    return jobList && !jobList->Jobs().empty();
}

void TChunkReplicator::ScheduleChunkRefresh(const TChunkId& chunkId)
{
    auto chunkManager = Bootstrap->GetChunkManager();
    auto* chunk = chunkManager->FindChunk(chunkId);
    if (IsObjectAlive(chunk)) {
        ScheduleChunkRefresh(chunk);
    }
}

void TChunkReplicator::ScheduleChunkRefresh(TChunk* chunk)
{
    if (!IsObjectAlive(chunk) || chunk->GetRefreshScheduled())
        return;

    TRefreshEntry entry;
    entry.Chunk = chunk;
    entry.When = GetCpuInstant() + ChunkRefreshDelay;
    RefreshList.push_back(entry);
    chunk->SetRefreshScheduled(true);

    auto objectManager = Bootstrap->GetObjectManager();
    objectManager->LockObject(chunk);
}

void TChunkReplicator::OnRefresh()
{
    if (RefreshList.empty()) {
        return;
    }

    auto objectManager = Bootstrap->GetObjectManager();

    int count = 0;
    PROFILE_TIMING ("/incremental_refresh_time") {
        auto chunkManager = Bootstrap->GetChunkManager();
        auto now = GetCpuInstant();
        for (int i = 0; i < Config->MaxChunksPerRefresh; ++i) {
            if (RefreshList.empty())
                break;

            const auto& entry = RefreshList.front();
            if (entry.When > now)
                break;

            auto* chunk = entry.Chunk;
            RefreshList.pop_front();
            chunk->SetRefreshScheduled(false);
            ++count;

            if (IsObjectAlive(chunk)) {
                Refresh(chunk);
            }

            objectManager->UnlockObject(chunk);
        }
    }

    LOG_DEBUG("Incremental chunk refresh completed, %d chunks processed",
        count);
}

bool TChunkReplicator::IsEnabled()
{
    // This method also logs state changes.

    auto chunkManager = Bootstrap->GetChunkManager();
    auto nodeTracker = Bootstrap->GetNodeTracker();

    auto config = Config->ChunkReplicator;
    if (config->MinOnlineNodeCount) {
        int needOnline = config->MinOnlineNodeCount.Get();
        int gotOnline = nodeTracker->GetOnlineNodeCount();
        if (gotOnline < needOnline) {
            if (!LastEnabled || LastEnabled.Get()) {
                LOG_INFO("Chunk replicator disabled: too few online nodes, needed >= %d but got %d",
                    needOnline,
                    gotOnline);
                LastEnabled = false;
            }
            return false;
        }
    }

    int chunkCount = chunkManager->GetChunkCount();
    int lostChunkCount = chunkManager->LostChunks().size();
    if (config->MaxLostChunkFraction && chunkCount > 0) {
        double needFraction = config->MaxLostChunkFraction.Get();
        double gotFraction = (double) lostChunkCount / chunkCount;
        if (gotFraction > needFraction) {
            if (!LastEnabled || LastEnabled.Get()) {
                LOG_INFO("Chunk replicator disabled: too many lost chunks, needed <= %lf but got %lf",
                    needFraction,
                    gotFraction);
                LastEnabled = false;
            }
            return false;
        }
    }

    if (!LastEnabled || !LastEnabled.Get()) {
        LOG_INFO("Chunk replicator enabled");
        LastEnabled = true;
    }

    return true;
}

int TChunkReplicator::GetRefreshListSize() const
{
    return static_cast<int>(RefreshList.size());
}

int TChunkReplicator::GetRFUpdateListSize() const
{
    return static_cast<int>(RFUpdateList.size());
}

void TChunkReplicator::ScheduleRFUpdate(TChunkTree* chunkTree)
{
    switch (chunkTree->GetType()) {
        case EObjectType::Chunk:
            ScheduleRFUpdate(chunkTree->AsChunk());
            break;
        case EObjectType::ChunkList:
            ScheduleRFUpdate(chunkTree->AsChunkList());
            break;
        default:
            YUNREACHABLE();
    }
}

void TChunkReplicator::ScheduleRFUpdate(TChunkList* chunkList)
{
    class TVisitor
        : public IChunkVisitor
    {
    public:
        TVisitor(
            NCellMaster::TBootstrap* bootstrap,
            TChunkReplicatorPtr replicator,
            TChunkList* root)
            : Bootstrap(bootstrap)
            , Replicator(std::move(replicator))
            , Root(root)
        { }

        void Run()
        {
            TraverseChunkTree(Bootstrap, this, Root);
        }

    private:
        TBootstrap* Bootstrap;
        TChunkReplicatorPtr Replicator;
        TChunkList* Root;

        virtual bool OnChunk(
            TChunk* chunk,
            const NChunkClient::NProto::TReadLimit& startLimit,
            const NChunkClient::NProto::TReadLimit& endLimit) override
        {
            UNUSED(startLimit);
            UNUSED(endLimit);

            Replicator->ScheduleRFUpdate(chunk);
            return true;
        }

        virtual void OnError(const TError& error) override
        {
            LOG_ERROR(error, "Error traversing chunk tree for RF update");
        }

        virtual void OnFinish() override
        { }

    };

    New<TVisitor>(Bootstrap, this, chunkList)->Run();
}

void TChunkReplicator::ScheduleRFUpdate(TChunk* chunk)
{
    if (!IsObjectAlive(chunk) || chunk->GetRFUpdateScheduled())
        return;

    RFUpdateList.push_back(chunk);
    chunk->SetRFUpdateScheduled(true);

    auto objectManager = Bootstrap->GetObjectManager();
    objectManager->LockObject(chunk);
}

void TChunkReplicator::OnRFUpdate()
{
    if (RFUpdateList.empty() ||
        !Bootstrap->GetMetaStateFacade()->GetManager()->HasActiveQuorum())
    {
        RFUpdateInvoker->ScheduleNext();
        return;
    }

    // Extract up to GCObjectsPerMutation objects and post a mutation.
    auto chunkManager = Bootstrap->GetChunkManager();
    auto objectManager = Bootstrap->GetObjectManager();
    NProto::TMetaReqUpdateChunkReplicationFactor request;

    PROFILE_TIMING ("/rf_update_time") {
        for (int i = 0; i < Config->MaxChunksPerRFUpdate; ++i) {
            if (RFUpdateList.empty())
                break;

            auto* chunk = RFUpdateList.front();
            RFUpdateList.pop_front();
            chunk->SetRFUpdateScheduled(false);

            if (IsObjectAlive(chunk)) {
                int replicationFactor = ComputeReplicationFactor(chunk);
                if (chunk->GetReplicationFactor() != replicationFactor) {
                    auto* update = request.add_updates();
                    ToProto(update->mutable_chunk_id(), chunk->GetId());
                    update->set_replication_factor(replicationFactor);
                }
            }

            objectManager->UnlockObject(chunk);
        }
    }

    if (request.updates_size() == 0) {
        RFUpdateInvoker->ScheduleNext();
        return;
    }

    LOG_DEBUG("Starting RF update for %d chunks", request.updates_size());

    auto invoker = Bootstrap->GetMetaStateFacade()->GetEpochInvoker();
    chunkManager
        ->CreateUpdateChunkReplicationFactorMutation(request)
        ->OnSuccess(BIND(&TChunkReplicator::OnRFUpdateCommitSucceeded, MakeWeak(this)).Via(invoker))
        ->OnError(BIND(&TChunkReplicator::OnRFUpdateCommitFailed, MakeWeak(this)).Via(invoker))
        ->PostCommit();
}

void TChunkReplicator::OnRFUpdateCommitSucceeded()
{
    LOG_DEBUG("RF update commit succeeded");

    RFUpdateInvoker->ScheduleOutOfBand();
    RFUpdateInvoker->ScheduleNext();
}

void TChunkReplicator::OnRFUpdateCommitFailed(const TError& error)
{
    LOG_WARNING(error, "RF update commit failed");

    RFUpdateInvoker->ScheduleNext();
}

int TChunkReplicator::ComputeReplicationFactor(const TChunk* chunk)
{
    int result = 0;

    // Unique number used to distinguish already visited chunk lists.
    auto mark = TChunkList::GenerateVisitMark();

    // BFS queue. Try to avoid allocations.
    TSmallVector<TChunkList*, 64> queue;
    size_t frontIndex = 0;

    auto enqueue = [&] (TChunkList* chunkList) {
        if (chunkList->GetVisitMark() != mark) {
            chunkList->SetVisitMark(mark);
            queue.push_back(chunkList);
        }
    };

    // Put seeds into the queue.
    FOREACH (auto* parent, chunk->Parents()) {
        auto* adjustedParent = FollowParentLinks(parent);
        if (adjustedParent) {
            enqueue(adjustedParent);
        }
    }

    // The main BFS loop.
    while (frontIndex < queue.size()) {
        auto* chunkList = queue[frontIndex++];

        // Examine owners, if any.
        FOREACH (const auto* owningNode, chunkList->OwningNodes()) {
            result = std::max(result, owningNode->GetOwningReplicationFactor());
        }

        // Proceed to parents.
        FOREACH (auto* parent, chunkList->Parents()) {
            auto* adjustedParent = FollowParentLinks(parent);
            if (adjustedParent) {
                enqueue(adjustedParent);
            }
        }
    }

    return result == 0 ? chunk->GetReplicationFactor() : result;
}

TChunkList* TChunkReplicator::FollowParentLinks(TChunkList* chunkList)
{
    while (chunkList->OwningNodes().empty()) {
        const auto& parents = chunkList->Parents();
        size_t parentCount = parents.size();
        if (parentCount == 0) {
            return nullptr;
        }
        if (parentCount > 1) {
            break;
        }
        chunkList = *parents.begin();
    }
    return chunkList;
}

void TChunkReplicator::RegisterJob(TJobPtr job)
{
    LOG_INFO("Job registered (JobId: %s, JobType: %s, Address: %s)",
        ~ToString(job->GetJobId()),
        ~job->GetType().ToString(),
        ~job->GetNode()->GetAddress());

    YCHECK(JobMap.insert(std::make_pair(job->GetJobId(), job)).second);
    
    job->GetNode()->AddJob(job);

    const auto& chunkId = job->GetChunkId();
    auto jobList = FindJobList(chunkId);
    if (!jobList) {
        jobList = New<TJobList>(chunkId);
        YCHECK(JobListMap.insert(std::make_pair(chunkId, jobList)).second);
    }
    jobList->AddJob(job);
}

void TChunkReplicator::UnregisterJob(TJobPtr job)
{
    LOG_INFO("Job unregistered (JobId: %s, Address: %s)",
        ~ToString(job->GetJobId()),
        ~job->GetNode()->GetAddress());
    
    YCHECK(JobMap.erase(job->GetJobId()) == 1);

    job->GetNode()->RemoveJob(job);

    const auto& chunkId = job->GetChunkId();
    auto jobList = FindJobList(chunkId);
    YCHECK(jobList);
    jobList->RemoveJob(job);
    if (jobList->Jobs().empty()) {
        YCHECK(JobListMap.erase(chunkId) == 1);
    }

    ScheduleChunkRefresh(job->GetChunkId());
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT
