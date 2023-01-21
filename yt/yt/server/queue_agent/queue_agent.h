#pragma once

#include "object.h"

#include <yt/yt/server/lib/cypress_election/public.h>

#include <yt/yt/ytlib/api/native/public.h>

#include <yt/yt/ytlib/hive/public.h>

#include <yt/yt/ytlib/queue_client/dynamic_state.h>

#include <yt/yt/core/ytree/public.h>

#include <yt/yt/core/rpc/bus/public.h>

namespace NYT::NQueueAgent {

////////////////////////////////////////////////////////////////////////////////

struct TClusterProfilingCounters
{
    NProfiling::TGauge Queues;
    NProfiling::TGauge Consumers;
    NProfiling::TGauge Partitions;

    explicit TClusterProfilingCounters(NProfiling::TProfiler profiler);
};

struct TGlobalProfilingCounters
{
    NProfiling::TGauge Registrations;

    explicit TGlobalProfilingCounters(NProfiling::TProfiler profiler);
};

//! Object responsible for tracking the list of queues assigned to this particular controller.
class TQueueAgent
    : public IObjectStore
{
public:
    TQueueAgent(
        TQueueAgentConfigPtr config,
        NApi::NNative::IConnectionPtr nativeConnection,
        NHiveClient::TClientDirectoryPtr clientDirectory,
        IInvokerPtr controlInvoker,
        NQueueClient::TDynamicStatePtr dynamicState,
        NCypressElection::ICypressElectionManagerPtr electionManager,
        TString agentId);

    void Start();

    NYTree::IMapNodePtr GetOrchidNode() const;

    void OnDynamicConfigChanged(
        const TQueueAgentDynamicConfigPtr& oldConfig,
        const TQueueAgentDynamicConfigPtr& newConfig);

    void PopulateAlerts(std::vector<TError>* alerts) const;

    // IObjectStore implementation.

    TRefCountedPtr FindSnapshot(NQueueClient::TCrossClusterReference objectRef) const override;

    std::vector<NQueueClient::TConsumerRegistrationTableRow> GetRegistrations(
        NQueueClient::TCrossClusterReference objectRef,
        EObjectKind objectKind) const override;

private:
    const TQueueAgentConfigPtr Config_;
    TQueueAgentDynamicConfigPtr DynamicConfig_;
    const NHiveClient::TClientDirectoryPtr ClientDirectory_;
    const IInvokerPtr ControlInvoker_;
    const NQueueClient::TDynamicStatePtr DynamicState_;
    const NCypressElection::ICypressElectionManagerPtr ElectionManager_;
    const NConcurrency::IThreadPoolPtr ControllerThreadPool_;
    const NConcurrency::TPeriodicExecutorPtr PassExecutor_;

    const TString AgentId_;

    THashMap<TString, TClusterProfilingCounters> ClusterProfilingCounters_;
    TGlobalProfilingCounters GlobalProfilingCounters_;

    std::atomic<bool> Active_ = false;

    struct TObject
    {
        IObjectControllerPtr Controller;
        std::vector<NQueueClient::TConsumerRegistrationTableRow> Registrations;
    };
    using TObjectMap = THashMap<NQueueClient::TCrossClusterReference, TObject>;

    mutable NThreading::TReaderWriterSpinLock ObjectLock_;
    TEnumIndexedVector<EObjectKind, TObjectMap> Objects_;
    THashMap<NQueueClient::TCrossClusterReference, TString> ObjectToHost_;

    //! Current pass error if any.
    TError PassError_;
    //! Current poll iteration instant.
    TInstant PassInstant_ = TInstant::Zero();
    //! Index of the current poll iteration.
    i64 PassIndex_ = -1;

    NRpc::IChannelFactoryPtr QueueAgentChannelFactory_;

    TEnumIndexedVector<EObjectKind, NYTree::INodePtr> ObjectServiceNodes_;

    std::vector<TError> Alerts_;

    NYTree::IYPathServicePtr RedirectYPathRequest(const TString& host, TStringBuf queryRoot, TStringBuf key) const;

    void BuildObjectYson(
        EObjectKind objectKind,
        const NQueueClient::TCrossClusterReference& objectRef,
        const IObjectControllerPtr& object,
        NYson::IYsonConsumer* ysonConsumer) const;

    //! One iteration of state polling and object store updating.
    void Pass();

    //! Stops periodic passes and destroys all controllers.
    void DoStop();

    void DoPopulateAlerts(std::vector<TError>* alerts) const;

    TClusterProfilingCounters& GetOrCreateClusterProfilingCounters(TString cluster);

    void Profile();

    friend class TObjectMapBoundService;
};

DEFINE_REFCOUNTED_TYPE(TQueueAgent)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NQueueAgent
