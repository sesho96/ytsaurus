#pragma once

#include "private.h"
#include "config.h"
#include "operation_controller.h"
#include "chunk_pool.h"
#include "chunk_list_pool.h"
#include "job_resources.h"
#include "serialize.h"
#include "event_log.h"

#include <core/misc/nullable.h>
#include <core/misc/id_generator.h>

#include <core/concurrency/periodic_executor.h>
#include <core/concurrency/rw_spinlock.h>
#include <core/concurrency/thread_affinity.h>

#include <core/actions/cancelable_context.h>

#include <core/ytree/ypath_client.h>

#include <core/yson/string.h>

#include <core/logging/log.h>

#include <ytlib/chunk_client/chunk_owner_ypath_proxy.h>

#include <ytlib/table_client/table_ypath_proxy.h>
#include <ytlib/table_client/unversioned_row.h>

#include <ytlib/file_client/file_ypath_proxy.h>

#include <ytlib/cypress_client/public.h>

#include <ytlib/chunk_client/public.h>
#include <ytlib/chunk_client/chunk_service_proxy.h>

#include <ytlib/node_tracker_client/public.h>
#include <ytlib/node_tracker_client/helpers.h>

#include <ytlib/job_tracker_client/statistics.h>

#include <server/chunk_server/public.h>

namespace NYT {
namespace NScheduler {

////////////////////////////////////////////////////////////////////

//! Describes which part of the operation needs a particular file.
DEFINE_ENUM(EOperationStage,
    (None)
    (Map)
    (ReduceCombiner)
    (Reduce)
);

DEFINE_ENUM(EInputChunkState,
    (Active)
    (Skipped)
    (Waiting)
);

DEFINE_ENUM(EJobReinstallReason,
    (Failed)
    (Aborted)
);

DEFINE_ENUM(EControllerState,
    (Preparing)
    (Running)
    (Finished)
);


class TOperationControllerBase
    : public IOperationController
    , public NPhoenix::IPersistent
    , public NPhoenix::TFactoryTag<NPhoenix::TNullFactory>
{
public:
    TOperationControllerBase(
        TSchedulerConfigPtr config,
        TOperationSpecBasePtr spec,
        IOperationHost* host,
        TOperation* operation);

    virtual void Initialize() override;
    virtual void Essentiate() override;
    virtual void Prepare() override;
    virtual void SaveSnapshot(TOutputStream* output) override;
    virtual void Revive() override;
    virtual void Commit() override;

    virtual void OnJobRunning(const TJobId& jobId, const NJobTrackerClient::NProto::TJobStatus& status) override;
    virtual void OnJobCompleted(const TCompletedJobSummary& jobSummary) override;
    virtual void OnJobFailed(const TFailedJobSummary& jobSummary) override;
    virtual void OnJobAborted(const TAbortedJobSummary& jobSummary) override;

    virtual void Abort() override;

    virtual TJobId ScheduleJob(
        ISchedulingContext* context,
        const NNodeTrackerClient::NProto::TNodeResources& jobLimits) override;

    virtual TCancelableContextPtr GetCancelableContext() const override;
    virtual IInvokerPtr GetCancelableControlInvoker() const override;
    virtual IInvokerPtr GetCancelableInvoker() const override;
    virtual IInvokerPtr GetInvoker() const override;

    virtual TFuture<void> Suspend() override;
    virtual void Resume() override;

    virtual int GetPendingJobCount() const override;
    virtual int GetTotalJobCount() const override;
    virtual NNodeTrackerClient::NProto::TNodeResources GetNeededResources() const override;

    virtual void BuildProgress(NYson::IYsonConsumer* consumer) const override;
    virtual void BuildBriefProgress(NYson::IYsonConsumer* consumer) const override;
    virtual void BuildResult(NYson::IYsonConsumer* consumer) const override;
    virtual void BuildBriefSpec(NYson::IYsonConsumer* consumer) const override;

    virtual void Persist(TPersistenceContext& context) override;

protected:
    // Forward declarations.
    class TTask;
    typedef TIntrusivePtr<TTask> TTaskPtr;

    struct TTaskGroup;
    typedef TIntrusivePtr<TTaskGroup> TTaskGroupPtr;

    struct TJoblet;
    typedef TIntrusivePtr<TJoblet> TJobletPtr;

    struct TCompletedJob;
    typedef TIntrusivePtr<TCompletedJob> TCompletedJobPtr;


    TSchedulerConfigPtr Config;
    IOperationHost* Host;
    TOperation* Operation;

    const TOperationId OperationId;

    NApi::IClientPtr AuthenticatedMasterClient;
    NApi::IClientPtr AuthenticatedInputMasterClient;
    NApi::IClientPtr AuthenticatedOutputMasterClient;

    mutable NLogging::TLogger Logger;

    TCancelableContextPtr CancelableContext;
    IInvokerPtr CancelableControlInvoker;
    IInvokerPtr Invoker;
    ISuspendableInvokerPtr SuspendableInvoker;
    IInvokerPtr CancelableInvoker;

    EControllerState State;
    NConcurrency::TReaderWriterSpinLock StateLock;

    // These totals are approximate.
    int TotalEstimatedInputChunkCount;
    i64 TotalEstimatedInputDataSize;
    i64 TotalEstimatedInputRowCount;
    i64 TotalEstimatedInputValueCount;
    i64 TotalEstimatedCompressedDataSize;

    int UnavailableInputChunkCount;

    // Job counters.
    TProgressCounter JobCounter;

    // Maps node ids to descriptors for job input chunks.
    NNodeTrackerClient::TNodeDirectoryPtr InputNodeDirectory;
    // Maps node ids to descriptors for job auxiliary chunks.
    NNodeTrackerClient::TNodeDirectoryPtr AuxNodeDirectory;

    // Operation transactions ids are stored here to avoid their retrieval from
    // potentially dangling Operation pointer.
    NObjectClient::TTransactionId AsyncSchedulerTransactionId;
    NObjectClient::TTransactionId SyncSchedulerTransactionId;
    NObjectClient::TTransactionId InputTransactionId;
    NObjectClient::TTransactionId OutputTransactionId;

    struct TUserObjectBase
    {
        NYPath::TRichYPath Path;
        NObjectClient::TObjectId ObjectId;
        NObjectClient::TCellTag CellTag;

        void Persist(TPersistenceContext& context);
    };


    struct TLivePreviewTableBase
    {
        // Live preview table id.
        NCypressClient::TNodeId LivePreviewTableId;

        // Chunk list for appending live preview results.
        NChunkClient::TChunkListId LivePreviewChunkListId;

        void Persist(TPersistenceContext& context);
    };

    struct TInputTable
        : public TUserObjectBase
    {
        //! Number of chunks in the whole table (without range selectors).
        int ChunkCount = -1;
        std::vector<NChunkClient::TRefCountedChunkSpecPtr> Chunks;
        NTableClient::TKeyColumns KeyColumns;

        void Persist(TPersistenceContext& context);
    };

    std::vector<TInputTable> InputTables;


    struct TJobBoundaryKeys
    {
        NTableClient::TOwningKey MinKey;
        NTableClient::TOwningKey MaxKey;
        int ChunkTreeKey;

        void Persist(TPersistenceContext& context);

    };

    struct TOutputTable
        : public TUserObjectBase
        , public TLivePreviewTableBase
    {
        bool AppendRequested = false;
        NChunkClient::EUpdateMode UpdateMode = NChunkClient::EUpdateMode::Overwrite;
        NCypressClient::ELockMode LockMode = NCypressClient::ELockMode::Exclusive;
        NTableClient::TTableWriterOptionsPtr Options = New<NTableClient::TTableWriterOptions>();
        NTableClient::TKeyColumns KeyColumns;
        bool ChunkPropertiesUpdateNeeded = false;

        // Server-side upload transaction.
        NTransactionClient::TTransactionId UploadTransactionId;

        // Chunk list for appending the output.
        NChunkClient::TChunkListId OutputChunkListId;

        // Statistics returned by EndUpload call.
        NChunkClient::NProto::TDataStatistics DataStatistics;

        //! Chunk trees comprising the output (the order matters).
        //! Keys are used when the output is sorted (e.g. in sort operations).
        //! Trees are sorted w.r.t. key and appended to #OutputChunkListId.
        std::multimap<int, NChunkClient::TChunkTreeId> OutputChunkTreeIds;

        std::vector<TJobBoundaryKeys> BoundaryKeys;

        NYson::TYsonString EffectiveAcl;

        void Persist(TPersistenceContext& context);
    };

    std::vector<TOutputTable> OutputTables;


    struct TIntermediateTable
        : public TLivePreviewTableBase
    {
        void Persist(TPersistenceContext& context);
    };

    TIntermediateTable IntermediateTable;


    struct TUserFile
        : public TUserObjectBase
    {
        std::shared_ptr<NYTree::IAttributeDictionary> Attributes;
        EOperationStage Stage = EOperationStage::None;
        Stroka FileName;
        std::vector<NChunkClient::NProto::TChunkSpec> ChunkSpecs;
        NObjectClient::EObjectType Type = NObjectClient::EObjectType::Null;
        bool Executable = false;
        NYson::TYsonString Format;

        void Persist(TPersistenceContext& context);
    };

    std::vector<TUserFile> Files;

    struct TJoblet
        : public TIntrinsicRefCounted
    {
        //! For serialization only.
        TJoblet()
            : JobIndex(-1)
            , StartRowIndex(-1)
            , OutputCookie(-1)
            , MemoryReserveEnabled(true)
        { }

        TJoblet(TTaskPtr task, int jobIndex)
            : Task(task)
            , JobIndex(jobIndex)
            , StartRowIndex(-1)
            , OutputCookie(IChunkPoolOutput::NullCookie)
        { }

        TTaskPtr Task;
        int JobIndex;
        i64 StartRowIndex;

        TJobId JobId;
        EJobType JobType;

        Stroka Address;
        NNodeTrackerClient::TNodeId NodeId;

        NNodeTrackerClient::NProto::TNodeResources ResourceLimits;

        TChunkStripeListPtr InputStripeList;
        IChunkPoolOutput::TCookie OutputCookie;

        bool MemoryReserveEnabled;

        //! All chunk lists allocated for this job.
        /*!
         *  For jobs with intermediate output this list typically contains one element.
         *  For jobs with final output this list typically contains one element per each output table.
         */
        std::vector<NChunkClient::TChunkListId> ChunkListIds;

        void Persist(TPersistenceContext& context);
    };

    struct TCompletedJob
        : public TIntrinsicRefCounted
    {
        //! For persistence only.
        TCompletedJob()
            : IsLost(false)
            , DestinationPool(nullptr)
        { }

        TCompletedJob(
            const TJobId& jobId,
            TTaskPtr sourceTask,
            IChunkPoolOutput::TCookie outputCookie,
            IChunkPoolInput* destinationPool,
            IChunkPoolInput::TCookie inputCookie,
            const Stroka& address,
            NNodeTrackerClient::TNodeId nodeId)
            : IsLost(false)
            , JobId(jobId)
            , SourceTask(std::move(sourceTask))
            , OutputCookie(outputCookie)
            , DestinationPool(destinationPool)
            , InputCookie(inputCookie)
            , Address(address)
            , NodeId(nodeId)
        { }

        bool IsLost;

        TJobId JobId;

        TTaskPtr SourceTask;
        IChunkPoolOutput::TCookie OutputCookie;

        IChunkPoolInput* DestinationPool;
        IChunkPoolInput::TCookie InputCookie;

        Stroka Address;
        NNodeTrackerClient::TNodeId NodeId;

        void Persist(TPersistenceContext& context);

    };

    class TTask
        : public TRefCounted
        , public NPhoenix::IPersistent
    {
    public:
        //! For persistence only.
        TTask();
        explicit TTask(TOperationControllerBase* controller);

        void Initialize();

        virtual Stroka GetId() const = 0;
        virtual TTaskGroupPtr GetGroup() const = 0;

        virtual int GetPendingJobCount() const;
        int GetPendingJobCountDelta();

        virtual int GetTotalJobCount() const;
        int GetTotalJobCountDelta();

        virtual NNodeTrackerClient::NProto::TNodeResources GetTotalNeededResources() const;
        NNodeTrackerClient::NProto::TNodeResources GetTotalNeededResourcesDelta();

        virtual bool IsIntermediateOutput() const;

        virtual TDuration GetLocalityTimeout() const = 0;
        virtual i64 GetLocality(NNodeTrackerClient::TNodeId nodeId) const;
        virtual bool HasInputLocality() const;

        const NNodeTrackerClient::NProto::TNodeResources& GetMinNeededResources() const;
        virtual NNodeTrackerClient::NProto::TNodeResources GetNeededResources(TJobletPtr joblet) const;

        void ResetCachedMinNeededResources();

        DEFINE_BYVAL_RW_PROPERTY(TNullable<TInstant>, DelayedTime);

        void AddInput(TChunkStripePtr stripe);
        void AddInput(const std::vector<TChunkStripePtr>& stripes);
        void FinishInput();

        void CheckCompleted();

        TJobId ScheduleJob(
            ISchedulingContext* context,
            const NNodeTrackerClient::NProto::TNodeResources& jobLimits);

        virtual void OnJobCompleted(TJobletPtr joblet, const TCompletedJobSummary& jobSummary);
        virtual void OnJobFailed(TJobletPtr joblet, const TFailedJobSummary& jobSummary);
        virtual void OnJobAborted(TJobletPtr joblet, const TAbortedJobSummary& jobSummary);
        virtual void OnJobLost(TCompletedJobPtr completedJob);

        // First checks against a given node, then against all nodes if needed.
        void CheckResourceDemandSanity(
            const NNodeTrackerClient::NProto::TNodeResources& nodeResourceLimits,
            const NNodeTrackerClient::NProto::TNodeResources& neededResources);

        // Checks against all available nodes.
        void CheckResourceDemandSanity(
            const NNodeTrackerClient::NProto::TNodeResources& neededResources);

        void DoCheckResourceDemandSanity(const NNodeTrackerClient::NProto::TNodeResources& neededResources);

        bool IsPending() const;
        bool IsCompleted() const;

        virtual bool IsActive() const;

        i64 GetTotalDataSize() const;
        i64 GetCompletedDataSize() const;
        i64 GetPendingDataSize() const;

        virtual IChunkPoolInput* GetChunkPoolInput() const = 0;
        virtual IChunkPoolOutput* GetChunkPoolOutput() const = 0;

        virtual void Persist(TPersistenceContext& context) override;

    private:
        TOperationControllerBase* Controller;

        int CachedPendingJobCount;
        int CachedTotalJobCount;

        NNodeTrackerClient::NProto::TNodeResources CachedTotalNeededResources;
        mutable TNullable<NNodeTrackerClient::NProto::TNodeResources> CachedMinNeededResources;

        TInstant LastDemandSanityCheckTime;
        bool CompletedFired;

        //! For each lost job currently being replayed, maps output cookie to corresponding input cookie.
        yhash_map<IChunkPoolOutput::TCookie, IChunkPoolInput::TCookie> LostJobCookieMap;

    protected:
        NLogging::TLogger Logger;

        virtual NNodeTrackerClient::NProto::TNodeResources GetMinNeededResourcesHeavy() const = 0;

        virtual void OnTaskCompleted();

        virtual EJobType GetJobType() const = 0;
        virtual void PrepareJoblet(TJobletPtr joblet);
        virtual void BuildJobSpec(TJobletPtr joblet, NJobTrackerClient::NProto::TJobSpec* jobSpec) = 0;

        virtual void OnJobStarted(TJobletPtr joblet);

        virtual bool IsMemoryReserveEnabled() const = 0;
        virtual NTableClient::TTableReaderOptionsPtr GetTableReaderOptions() const = 0;

        void AddPendingHint();
        void AddLocalityHint(NNodeTrackerClient::TNodeId nodeId);

        void ReinstallJob(TJobletPtr joblet, EJobReinstallReason reason);

        void AddSequentialInputSpec(
            NJobTrackerClient::NProto::TJobSpec* jobSpec,
            TJobletPtr joblet);
        void AddParallelInputSpec(
            NJobTrackerClient::NProto::TJobSpec* jobSpec,
            TJobletPtr joblet);
        static void AddChunksToInputSpec(
            NNodeTrackerClient::TNodeDirectoryBuilder* directoryBuilder,
            NScheduler::NProto::TTableInputSpec* inputSpec,
            TChunkStripePtr stripe,
            TNullable<int> partitionTag);

        void AddFinalOutputSpecs(NJobTrackerClient::NProto::TJobSpec* jobSpec, TJobletPtr joblet);
        void AddIntermediateOutputSpec(
            NJobTrackerClient::NProto::TJobSpec* jobSpec,
            TJobletPtr joblet,
            const NTableClient::TKeyColumns& keyColumns);

        static void UpdateInputSpecTotals(
            NJobTrackerClient::NProto::TJobSpec* jobSpec,
            TJobletPtr joblet);

        void RegisterIntermediate(TJobletPtr joblet, TChunkStripePtr stripe, TTaskPtr destinationTask);
        void RegisterIntermediate(TJobletPtr joblet, TChunkStripePtr stripe, IChunkPoolInput* destinationPool);

        static TChunkStripePtr BuildIntermediateChunkStripe(
            google::protobuf::RepeatedPtrField<NChunkClient::NProto::TChunkSpec>* chunkSpecs);

        void RegisterOutput(TJobletPtr joblet, int key, const TCompletedJobSummary& jobSummary);

    };

    //! All tasks declared by calling #RegisterTask, mostly for debugging purposes.
    std::vector<TTaskPtr> Tasks;


    //! Groups provide means:
    //! - to prioritize tasks
    //! - to skip a vast number of tasks whose resource requirements cannot be met
    struct TTaskGroup
        : public TIntrinsicRefCounted
    {
        //! No task from this group is considered for scheduling unless this requirement is met.
        NNodeTrackerClient::NProto::TNodeResources MinNeededResources;

        //! All non-local tasks.
        yhash_set<TTaskPtr> NonLocalTasks;

        //! Non-local tasks that may possibly be ready (but a delayed check is still needed)
        //! keyed by min memory demand (as reported by TTask::GetMinNeededResources).
        std::multimap<i64, TTaskPtr> CandidateTasks;

        //! Non-local tasks keyed by deadline.
        std::multimap<TInstant, TTaskPtr> DelayedTasks;

        //! Local tasks keyed by node id.
        yhash_map<NNodeTrackerClient::TNodeId, yhash_set<TTaskPtr>> NodeIdToTasks;

        TTaskGroup()
        {
            MinNeededResources.set_user_slots(1);
        }

        void Persist(TPersistenceContext& context);

    };

    //! All task groups declared by calling #RegisterTaskGroup, in the order of decreasing priority.
    std::vector<TTaskGroupPtr> TaskGroups;

    void RegisterTask(TTaskPtr task);
    void RegisterTaskGroup(TTaskGroupPtr group);

    void UpdateTask(TTaskPtr task);

    void UpdateAllTasks();

    virtual void CustomizeJoblet(TJobletPtr joblet);
    virtual void CustomizeJobSpec(TJobletPtr joblet, NJobTrackerClient::NProto::TJobSpec* jobSpec);

    void DoAddTaskLocalityHint(TTaskPtr task, NNodeTrackerClient::TNodeId nodeId);
    void AddTaskLocalityHint(TTaskPtr task, NNodeTrackerClient::TNodeId nodeId);
    void AddTaskLocalityHint(TTaskPtr task, TChunkStripePtr stripe);
    void AddTaskPendingHint(TTaskPtr task);
    void ResetTaskLocalityDelays();

    void MoveTaskToCandidates(TTaskPtr task, std::multimap<i64, TTaskPtr>& candidateTasks);

    bool CheckJobLimits(
        TTaskPtr task,
        const NNodeTrackerClient::NProto::TNodeResources& jobLimits,
        const NNodeTrackerClient::NProto::TNodeResources& nodeResourceLimits);

    void CheckTimeLimit();

    TJobId DoScheduleJob(ISchedulingContext* context, const NNodeTrackerClient::NProto::TNodeResources& jobLimits);
    TJobId DoScheduleLocalJob(ISchedulingContext* context, const NNodeTrackerClient::NProto::TNodeResources& jobLimits);
    TJobId DoScheduleNonLocalJob(ISchedulingContext* context, const NNodeTrackerClient::NProto::TNodeResources& jobLimits);

    void OnJobStarted(const TJobId& jobId);

    DECLARE_THREAD_AFFINITY_SLOT(ControlThread);
    DECLARE_THREAD_AFFINITY_SLOT(BackgroundThread);


    // Jobs in progress management.
    void RegisterJoblet(TJobletPtr joblet);
    TJobletPtr GetJoblet(const TJobId& jobId);
    void RemoveJoblet(const TJobId& jobId);


    // Initialization.
    virtual void DoInitialize();
    virtual void InitializeTransactions();


    // Preparation.
    void DoPrepare();
    void GetInputTablesBasicAttributes();
    void GetOutputTablesBasicAttributes();
    void GetFilesBasicAttributes(std::vector<TUserFile>* files);
    void FetchInputTables();
    void LockInputTables();
    void BeginUploadOutputTables();
    void GetOutputTablesUploadParams();
    void FetchUserFiles(std::vector<TUserFile>* files);
    void LockUserFiles(std::vector<TUserFile>* files, const std::vector<Stroka>& attributeKeys);
    void CreateLivePreviewTables();
    void PrepareLivePreviewTablesForUpdate();
    void CollectTotals();
    virtual void CustomPrepare();
    void AddAllTaskPendingHints();
    void InitInputChunkScraper();
    void SuspendUnavailableInputStripes();
    void InitQuerySpec(
        NProto::TSchedulerJobSpecExt* schedulerJobSpecExt,
        const Stroka& queryString,
        const NQueryClient::TTableSchema& schema);

    void PickIntermediateDataCell();
    void InitChunkListPool();

    void ValidateKey(const NTableClient::TOwningKey& key);

    // Initialize transactions
    void StartAsyncSchedulerTransaction();
    void StartSyncSchedulerTransaction();
    virtual void StartInputTransaction(NObjectClient::TTransactionId parentTransactionId);
    virtual void StartOutputTransaction(NObjectClient::TTransactionId parentTransactionId);

    // Completion.
    void DoCommit();
    void TeleportOutputChunks();
    void AttachOutputChunks();
    void EndUploadOutputTables();
    virtual void CustomCommit();

    // Revival.
    void ReinstallLivePreview();
    void AbortAllJoblets();

    void DoSaveSnapshot(TOutputStream* output);
    void DoLoadSnapshot();

    //! Called to extract input table paths from the spec.
    virtual std::vector<NYPath::TRichYPath> GetInputTablePaths() const = 0;

    //! Called to extract output table paths from the spec.
    virtual std::vector<NYPath::TRichYPath> GetOutputTablePaths() const = 0;

    typedef std::pair<NYPath::TRichYPath, EOperationStage> TPathWithStage;

    //! Called to extract file paths from the spec.
    virtual std::vector<TPathWithStage> GetFilePaths() const;

    //! Called when a job is unable to read a chunk.
    void OnChunkFailed(const NChunkClient::TChunkId& chunkId);

    //! Called when a job is unable to read an intermediate chunk
    //! (i.e. that is not a part of the input).
    /*!
     *  The default implementation fails the operation immediately.
     *  Those operations providing some fault tolerance for intermediate chunks
     *  must override this method.
     */
    void OnIntermediateChunkUnavailable(const NChunkClient::TChunkId& chunkId);


    struct TStripeDescriptor
    {
        TChunkStripePtr Stripe;
        IChunkPoolInput::TCookie Cookie;
        TTaskPtr Task;

        TStripeDescriptor()
            : Cookie(IChunkPoolInput::NullCookie)
        { }

        void Persist(TPersistenceContext& context);

    };

    struct TInputChunkDescriptor
    {
        SmallVector<TStripeDescriptor, 1> InputStripes;
        SmallVector<NChunkClient::TRefCountedChunkSpecPtr, 1> ChunkSpecs;
        EInputChunkState State;

        TInputChunkDescriptor()
            : State(EInputChunkState::Active)
        { }

        void Persist(TPersistenceContext& context);

    };

    //! Callback called by TChunkScraper when get information on some chunk.
    void OnInputChunkLocated(
        const NChunkClient::TChunkId& chunkId,
        const NChunkClient::TChunkReplicaList& replicas);

    //! Called when a job is unable to read an input chunk or
    //! chunk scraper has encountered unavailable chunk.
    void OnInputChunkUnavailable(
        const NChunkClient::TChunkId& chunkId,
        TInputChunkDescriptor& descriptor);

    void OnInputChunkAvailable(
        const NChunkClient::TChunkId& chunkId,
        TInputChunkDescriptor& descriptor,
        const NChunkClient::TChunkReplicaList& replicas);

    virtual bool IsOutputLivePreviewSupported() const;
    virtual bool IsIntermediateLivePreviewSupported() const;

    virtual void OnOperationCompleted();
    virtual void OnOperationFailed(const TError& error);

    virtual bool IsCompleted() const = 0;

    void SetState(EControllerState state);

    //! Returns |true| when the controller is prepared.
    /*!
     *  Preparation happens in a controller thread.
     *  The state must not be touched from the control thread
     *  while this function returns |false|.
     */
    bool IsPrepared() const;

    //! Returns |true| as long as the operation can schedule new jobs.
    bool IsRunning() const;

    //! Returns |true| when operation completion event is scheduled to control invoker.
    bool IsFinished() const;

    // Unsorted helpers.

    //! Enables sorted output from user jobs.
    virtual bool IsSortedOutputSupported() const;

    //! Enables fetching all input replicas (not only data)
    virtual bool IsParityReplicasFetchEnabled() const;

    //! If |true| then all jobs started within the operation must
    //! preserve row count. This invariant is checked for each completed job.
    //! Should a violation be discovered, the operation fails.
    virtual bool IsRowCountPreserved() const;

    NTableClient::TKeyColumns CheckInputTablesSorted(
        const NTableClient::TKeyColumns& keyColumns);
    static bool CheckKeyColumnsCompatible(
        const NTableClient::TKeyColumns& fullColumns,
        const NTableClient::TKeyColumns& prefixColumns);
    //! Returns the longest common prefix of input table keys.
    NTableClient::TKeyColumns GetCommonInputKeyPrefix();

    void UpdateAllTasksIfNeeded(const TProgressCounter& jobCounter);
    bool IsMemoryReserveEnabled(const TProgressCounter& jobCounter) const;
    i64 GetMemoryReserve(bool memoryReserveEnabled, TUserJobSpecPtr userJobSpec) const;

    void RegisterInputStripe(TChunkStripePtr stripe, TTaskPtr task);


    void RegisterBoundaryKeys(
        const NTableClient::NProto::TBoundaryKeysExt& boundaryKeys,
        int key,
        TOutputTable* outputTable);

    virtual void RegisterOutput(TJobletPtr joblet, int key, const TCompletedJobSummary& jobSummary);

    void RegisterOutput(
        NChunkClient::TRefCountedChunkSpecPtr chunkSpec,
        int key,
        int tableIndex);

    void RegisterOutput(
        const NChunkClient::TChunkTreeId& chunkTreeId,
        int key,
        int tableIndex,
        TOutputTable& table);

    void RegisterIntermediate(
        TJobletPtr joblet,
        TCompletedJobPtr completedJob,
        TChunkStripePtr stripe);

    bool HasEnoughChunkLists(bool intermediate);
    NChunkClient::TChunkListId ExtractChunkList(NObjectClient::TCellTag cellTag);
    void ReleaseChunkLists(const std::vector<NChunkClient::TChunkListId>& ids);

    //! Returns the list of all input chunks collected from all input tables.
    std::vector<NChunkClient::TRefCountedChunkSpecPtr> CollectInputChunks() const;

    //! Converts a list of input chunks into a list of chunk stripes for further
    //! processing. Each stripe receives exactly one chunk (as suitable for most
    //! jobs except merge). The resulting stripes are of approximately equal
    //! size. The size per stripe is either |maxSliceDataSize| or
    //! |TotalEstimateInputDataSize / jobCount|, whichever is smaller. If the resulting
    //! list contains less than |jobCount| stripes then |jobCount| is decreased
    //! appropriately.
    std::vector<TChunkStripePtr> SliceChunks(
        const std::vector<NChunkClient::TRefCountedChunkSpecPtr>& chunkSpecs,
        i64 maxSliceDataSize,
        int* jobCount);

    std::vector<TChunkStripePtr> SliceInputChunks(
        i64 maxSliceDataSize,
        int* jobCount);

    int SuggestJobCount(
        i64 totalDataSize,
        i64 dataSizePerJob,
        TNullable<int> configJobCount,
        int maxJobCount) const;

    void InitUserJobSpecTemplate(
        NScheduler::NProto::TUserJobSpec* proto,
        TUserJobSpecPtr config,
        const std::vector<TUserFile>& files);

    void InitUserJobSpec(
        NScheduler::NProto::TUserJobSpec* proto,
        TJobletPtr joblet,
        i64 memoryReserve);

    // Amount of memory reserved for output table writers in job proxy.
    i64 GetFinalOutputIOMemorySize(TJobIOConfigPtr ioConfig) const;

    i64 GetFinalIOMemorySize(
        TJobIOConfigPtr ioConfig,
        const TChunkStripeStatisticsVector& stripeStatistics) const;

    static void InitIntermediateInputConfig(TJobIOConfigPtr config);

    static void InitIntermediateOutputConfig(TJobIOConfigPtr config);
    void InitFinalOutputConfig(TJobIOConfigPtr config);

    TFluentLogEvent LogEventFluently(ELogEventType eventType);
    TFluentLogEvent LogFinishedJobFluently(ELogEventType eventType, TJobPtr job);

    void ValidateUserFileCount(TUserJobSpecPtr spec, const Stroka& operation);

private:
    typedef TOperationControllerBase TThis;

    typedef yhash_map<NChunkClient::TChunkId, TInputChunkDescriptor> TInputChunkMap;

    //! Keeps information needed to maintain the liveness state of input chunks.
    TInputChunkMap InputChunkMap;

    TOperationSpecBasePtr Spec;

    NObjectClient::TCellTag IntermediateOutputCellTag = NObjectClient::InvalidCellTag;
    TChunkListPoolPtr ChunkListPool;
    yhash<NObjectClient::TCellTag, int> CellTagToOutputTableCount;

    std::atomic<int> CachedPendingJobCount;

    NNodeTrackerClient::NProto::TNodeResources CachedNeededResources;
    NConcurrency::TReaderWriterSpinLock CachedNeededResourcesLock;

    //! Maps an intermediate chunk id to its originating completed job.
    yhash_map<NChunkClient::TChunkId, TCompletedJobPtr> ChunkOriginMap;

    //! Maps scheduler's job ids to controller's joblets.
    //! NB: |TJobPtr -> TJobletPtr| mapping would be faster but
    //! it cannot be serialized that easily.
    yhash_map<TJobId, TJobletPtr> JobletMap;

    //! Used to distinguish already seen ChunkSpecs while building #InputChunkMap.
    yhash_set<NChunkClient::TRefCountedChunkSpecPtr> InputChunkSpecs;

    NChunkClient::TChunkScraperPtr InputChunkScraper;

    //! Increments each time a new job is scheduled.
    TIdGenerator JobIndexGenerator;

    //! Aggregates job statistics.
    NJobTrackerClient::TStatistics JobStatistics;

    //! Runs periodic time limit checks that fail operation on timeout.
    NConcurrency::TPeriodicExecutorPtr CheckTimeLimitExecutor;


    void UpdateJobStatistics(const TJobSummary& jobSummary);

    NApi::IClientPtr CreateClient();

    static const NProto::TUserJobResult* FindUserJobResult(const TRefCountedJobResultPtr& result);

    NTransactionClient::TTransactionManagerPtr GetTransactionManagerForTransaction(
        const NObjectClient::TTransactionId& transactionId);

    void IncreaseNeededResources(const NNodeTrackerClient::NProto::TNodeResources& resourcesDelta);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NScheduler
} // namespace NYT
