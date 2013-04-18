#include "stdafx.h"
#include "job.h"
#include "operation.h"
#include "exec_node.h"
#include "operation_controller.h"

namespace NYT {
namespace NScheduler {

using namespace NNodeTrackerClient::NProto;

////////////////////////////////////////////////////////////////////

TJob::TJob(
    const TJobId& id,
    EJobType type,
    TOperationPtr operation,
    TExecNodePtr node,
    TInstant startTime,
    const TNodeResources& resourceUsage,
    TJobSpecBuilder specBuilder)
    : Id_(id)
    , Type_(type)
    , Operation_(~operation)
    , Node_(node)
    , StartTime_(startTime)
    , State_(EJobState::Waiting)
    , ResourceUsage_(resourceUsage)
    , SpecBuilder_(std::move(specBuilder))
{ }

////////////////////////////////////////////////////////////////////

} // namespace NScheduler
} // namespace NYT

