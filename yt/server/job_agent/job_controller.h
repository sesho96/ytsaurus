#pragma once

#include "public.h"
#include "job.h"

#include <yt/server/cell_node/public.h>

#include <yt/ytlib/job_tracker_client/public.h>

#include <yt/core/yson/consumer.h>

#include <yt/core/actions/signal.h>

#include <yt/core/concurrency/throughput_throttler.h>
#include <yt/core/concurrency/periodic_executor.h>

#include <yt/core/profiling/profile_manager.h>

namespace NYT {
namespace NJobAgent {

////////////////////////////////////////////////////////////////////////////////

DEFINE_ENUM(EJobOrigin,
    ((Master)    (0))
    ((Scheduler) (1))
);

////////////////////////////////////////////////////////////////////////////////

//! Controls all jobs scheduled to run at this node.
/*!
 *   Maintains a map of jobs, allows new jobs to be started and existing jobs to be stopped.
 *   New jobs are constructed by means of per-type factories registered via #RegisterFactory.
 *
 */
class TJobController
    : public TRefCounted
{
public:
    DEFINE_SIGNAL(void(), ResourcesUpdated);
    DEFINE_BYREF_RW_PROPERTY(NNodeTrackerClient::NProto::TNodeResourceLimitsOverrides, ResourceLimitsOverrides);

public:
    TJobController(
        TJobControllerConfigPtr config,
        NCellNode::TBootstrap* bootstrap);

    //! Registers a factory for a given job type.
    void RegisterFactory(
        EJobType type,
        TJobFactory factory);

    //! Finds the job by its id, returns |nullptr| if no job is found.
    IJobPtr FindJob(const TJobId& jobId) const;

    //! Finds the job by its id, throws if no job is found.
    IJobPtr GetJobOrThrow(const TJobId& jobId) const;

    //! Returns the list of all currently known jobs.
    std::vector<IJobPtr> GetJobs() const;

    //! Returns the maximum allowed resource usage.
    NNodeTrackerClient::NProto::TNodeResources GetResourceLimits() const;

    //! Return the current resource usage.
    NNodeTrackerClient::NProto::TNodeResources GetResourceUsage(bool includeWaiting = true) const;

    //! Prepares a heartbeat request.
    void PrepareHeartbeatRequest(
        NObjectClient::TCellTag cellTag,
        NObjectClient::EObjectType jobObjectType,
        NJobTrackerClient::NProto::TReqHeartbeat* request);

    //! Handles heartbeat response, i.e. starts new jobs, aborts and removes old ones etc.
    void ProcessHeartbeatResponse(NJobTrackerClient::NProto::TRspHeartbeat* response);

    //! Orchid server.
    NYTree::IYPathServicePtr GetOrchidService();

private:
    const TJobControllerConfigPtr Config_;
    NCellNode::TBootstrap* const Bootstrap_;

    yhash_map<EJobType, TJobFactory> Factories_;
    yhash_map<TJobId, IJobPtr> Jobs_;

    bool StartScheduled_ = false;

    NConcurrency::IThroughputThrottlerPtr StatisticsThrottler_;

    NProfiling::TProfiler ResourceLimitsProfiler_;
    NProfiling::TProfiler ResourceUsageProfiler_;
    TEnumIndexedVector<NProfiling::TTagId, EJobOrigin> JobOriginToTag_;

    NConcurrency::TPeriodicExecutorPtr ProfilingExecutor_;

    //! Starts a new job.
    IJobPtr CreateJob(
        const TJobId& jobId,
        const TOperationId& operationId,
        const NNodeTrackerClient::NProto::TNodeResources& resourceLimits,
        NJobTrackerClient::NProto::TJobSpec&& jobSpec);

    //! Stops a job.
    /*!
     *  If the job is running, aborts it.
     */
    void AbortJob(IJobPtr job);

    //! Removes the job from the map.
    /*!
     *  It is illegal to call #Remove before the job is stopped.
     */
    void RemoveJob(IJobPtr job);

    TJobFactory GetFactory(EJobType type) const;

    void ScheduleStart();

    void OnResourcesUpdated(
        TWeakPtr<IJob> job,
        const NNodeTrackerClient::NProto::TNodeResources& resourceDelta);

    void StartWaitingJobs();

    //! Compares new usage with resource limits. Detects resource overdraft.
    bool CheckResourceUsageDelta(const NNodeTrackerClient::NProto::TNodeResources& delta);

    //! Returns |true| if a job with given #jobResources can be started.
    //! Takes special care with ReplicationDataSize and RepairDataSize enabling
    // an arbitrary large overdraft for the
    //! first job.
    bool HasEnoughResources(
        const NNodeTrackerClient::NProto::TNodeResources& jobResources,
        const NNodeTrackerClient::NProto::TNodeResources& usedResources);

    void BuildOrchid(NYson::IYsonConsumer* consumer) const;

    void OnProfiling();

    TEnumIndexedVector<int, EJobOrigin> GetJobCountByOrigin() const;
};

DEFINE_REFCOUNTED_TYPE(TJobController)

////////////////////////////////////////////////////////////////////////////////

} // namespace NJobAgent
} // namespace NYT
