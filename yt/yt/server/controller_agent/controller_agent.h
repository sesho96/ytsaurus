#pragma once

#include "private.h"

#include "operation_controller.h"

#include <yt/yt/ytlib/scheduler/public.h>

#include <yt/yt/ytlib/node_tracker_client/public.h>

#include <yt/yt/ytlib/transaction_client/public.h>

#include <yt/yt/ytlib/event_log/public.h>

#include <yt/yt/client/api/public.h>

#include <yt/yt/core/ytree/public.h>

#include <yt/yt/core/concurrency/public.h>

#include <yt/yt/core/actions/signal.h>

#include <library/cpp/yt/memory/ref.h>

namespace NYT::NControllerAgent {

////////////////////////////////////////////////////////////////////

struct TJobSpecRequest
{
    TOperationId OperationId;
    TJobId JobId;
};

////////////////////////////////////////////////////////////////////

/*!
 *  \note Thread affinity: Control unless noted otherwise
 */
class TControllerAgent
    : public TRefCounted
{
public:
    TControllerAgent(
        TControllerAgentConfigPtr config,
        NYTree::INodePtr configNode,
        TBootstrap* bootstrap);
    ~TControllerAgent();

    void Initialize();

    /*!
     *  \note Thread affinity: any
     */
    NYTree::IYPathServicePtr CreateOrchidService();

    /*!
     *  \note Thread affinity: any
     */
    const IInvokerPtr& GetControllerThreadPoolInvoker();

    /*!
     *  \note Thread affinity: any
     */
    const IInvokerPtr& GetJobSpecBuildPoolInvoker();

    /*!
     *  \note Thread affinity: any
     */
    const IInvokerPtr& GetExecNodesUpdateInvoker();

    /*!
     *  \note Thread affinity: any
     */
    const IInvokerPtr& GetSnapshotIOInvoker();

    /*!
     *  \note Thread affinity: any
     */
    const NChunkClient::TThrottlerManagerPtr& GetChunkLocationThrottlerManager() const;

    /*!
     *  \note Thread affinity: any
     */
    const ICoreDumperPtr& GetCoreDumper() const;

    /*!
     *  \note Thread affinity: any
     */
    const NConcurrency::TAsyncSemaphorePtr& GetCoreSemaphore() const;

    /*!
     *  \note Thread affinity: any
     */
    const NEventLog::IEventLogWriterPtr& GetEventLogWriter() const;

    /*!
     *  \note Thread affinity: any
     */
    const NJobAgent::TJobReporterPtr& GetJobReporter() const;

    /*!
     * \note Thread affinity: any
     */
    TMemoryTagQueue* GetMemoryTagQueue();

    /*!
     *  \note Thread affinity: any
     */
    TMasterConnector* GetMasterConnector();

    /*!
     *  \note Thread affinity: any
     */
    TJobProfiler* GetJobProfiler() const;

    bool IsConnected() const;
    TIncarnationId GetIncarnationId() const;

    /*!
     *  \note Thread affinity: any
     */
    TInstant GetConnectionTime() const;

    void ValidateConnected() const;
    void ValidateIncarnation(TIncarnationId incarnationId) const;

    void Disconnect(const TError& error);

    const TControllerAgentConfigPtr& GetConfig() const;
    void UpdateConfig(const TControllerAgentConfigPtr& config);

    TOperationPtr FindOperation(TOperationId operationId);
    TOperationPtr GetOperation(TOperationId operationId);
    TOperationPtr GetOperationOrThrow(TOperationId operationId);
    const TOperationIdToOperationMap& GetOperations();

    void RegisterOperation(const NProto::TOperationDescriptor& descriptor);
    TFuture<TOperationControllerUnregisterResult> DisposeAndUnregisterOperation(TOperationId operationId);
    TFuture<void> UpdateOperationRuntimeParameters(
        TOperationId operationId,
        NScheduler::TOperationRuntimeParametersUpdatePtr update);
    TFuture<std::optional<TOperationControllerInitializeResult>> InitializeOperation(
        const TOperationPtr& operation,
        const std::optional<TControllerTransactionIds>& transactions);
    TFuture<std::optional<TOperationControllerPrepareResult>> PrepareOperation(const TOperationPtr& operation);
    TFuture<std::optional<TOperationControllerMaterializeResult>> MaterializeOperation(const TOperationPtr& operation);
    TFuture<std::optional<TOperationControllerReviveResult>> ReviveOperation(const TOperationPtr& operation);
    TFuture<std::optional<TOperationControllerCommitResult>> CommitOperation(const TOperationPtr& operation);
    TFuture<void> CompleteOperation(const TOperationPtr& operation);
    TFuture<void> TerminateOperation(const TOperationPtr& operation, EControllerState controllerFinalState);

    //! Extracts specs for given jobs; nulls indicate failures (e.g. missing jobs).
    TFuture<std::vector<TErrorOr<TSharedRef>>> ExtractJobSpecs(const std::vector<TJobSpecRequest>& requests);

    TFuture<TOperationInfo> BuildOperationInfo(TOperationId operationId);
    TFuture<NYson::TYsonString> BuildJobInfo(TOperationId operationId, TJobId jobId);

    //! Returns the total number of online exec nodes.
    /*!
     *  \note Thread affinity: any
     */
    int GetOnlineExecNodeCount() const;

    //! Returns the descriptors of online exec nodes matching a given #filter.
    /*!
     *  \note Thread affinity: any
     */
    NScheduler::TRefCountedExecNodeDescriptorMapPtr GetExecNodeDescriptors(const NScheduler::TSchedulingTagFilter& filter, bool onlineOnly = false) const;

    //! Returns maximum available resources of a node matching a given #filter.
    /*!
     *  \note Thread affinity: any
     */
    TJobResources GetMaxAvailableResources(const NScheduler::TSchedulingTagFilter& filter) const;

    /*!
     *  \note Thread affinity: any
     */
    const NConcurrency::IThroughputThrottlerPtr& GetJobSpecSliceThrottler() const;

    /*!
     *  \note Thread affinity: ControlThread
     */
    void ValidateOperationAccess(
        const TString& user,
        TOperationId operationId,
        NYTree::EPermission permission);

    //! Registers job for monitoring.
    /*!
     *  \returns job descriptor for the corresponding monitoring tag
     *           or nullopt if monitored job limit is reached.
     *  \note Thread affinity: any
     */
    std::optional<TString> RegisterJobForMonitoring(TOperationId operationId, TJobId jobId);

    //! Unregisters job monitoring.
    /*!
     *  \returns true iff the job was actually monitored.
     *
     *  \note Thread affinity: any
     */
    bool UnregisterJobForMonitoring(TOperationId operationId, TJobId jobId);

    //! Schedule job monitoring alert update.
    /*!
     *  \note Thread affinity: any
     */
    void EnqueueJobMonitoringAlertUpdate();

    //! Raised when connection prcoess starts.
    //! Subscribers may throw and yield.
    DECLARE_SIGNAL(void(), SchedulerConnecting);

    //! Raised when connection is complete.
    //! Subscribers may throw but cannot yield.
    DECLARE_SIGNAL(void(), SchedulerConnected);

    //! Raised when disconnect happens.
    //! Subscribers cannot neither throw nor yield
    DECLARE_SIGNAL(void(), SchedulerDisconnected);

private:
    class TImpl;
    const TIntrusivePtr<TImpl> Impl_;
};

DEFINE_REFCOUNTED_TYPE(TControllerAgent)

////////////////////////////////////////////////////////////////////

} // namespace NYT::NScheduler
