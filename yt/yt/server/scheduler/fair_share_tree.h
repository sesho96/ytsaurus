#pragma once

#include "private.h"
#include "scheduler_tree.h"

namespace NYT::NScheduler {

////////////////////////////////////////////////////////////////////////////////

class TFairShareStrategyOperationState
    : public TRefCounted
{
public:
    using TTreeIdToPoolNameMap = THashMap<TString, TPoolName>;

    DEFINE_BYVAL_RO_PROPERTY(IOperationStrategyHost*, Host);
    DEFINE_BYVAL_RO_PROPERTY(TFairShareStrategyOperationControllerPtr, Controller);
    DEFINE_BYREF_RW_PROPERTY(TTreeIdToPoolNameMap, TreeIdToPoolNameMap);
    DEFINE_BYVAL_RW_PROPERTY(bool, Enabled);

public:
    TFairShareStrategyOperationState(
        IOperationStrategyHost* host,
        const TFairShareStrategyOperationControllerConfigPtr& config);

    void UpdateConfig(const TFairShareStrategyOperationControllerConfigPtr& config);

    TPoolName GetPoolNameByTreeId(const TString& treeId) const;
};

DEFINE_REFCOUNTED_TYPE(TFairShareStrategyOperationState)

THashMap<TString, TPoolName> GetOperationPools(const TOperationRuntimeParametersPtr& runtimeParameters);

////////////////////////////////////////////////////////////////////////////////

template <class TFairShareImpl>
ISchedulerTreePtr CreateFairShareTree(
    TFairShareStrategyTreeConfigPtr config,
    TFairShareStrategyOperationControllerConfigPtr controllerConfig,
    ISchedulerStrategyHost* strategyHost,
    ISchedulerTreeHost* treeHost,
    std::vector<IInvokerPtr> feasibleInvokers,
    TString treeId);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NScheduler
