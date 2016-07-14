#include "exec_node.h"
#include "job_resources.h"

#include <yt/ytlib/node_tracker_client/helpers.h>

namespace NYT {
namespace NScheduler {

using namespace NNodeTrackerClient;
using namespace NNodeTrackerServer;
using namespace NConcurrency;

////////////////////////////////////////////////////////////////////

TExecNode::TExecNode(
    TNodeId id,
    const TNodeDescriptor& nodeDescriptor)
    : Id_(id)
    , MasterState_(ENodeState::Offline)
    , HasOngoingHeartbeat_(false)
    , HasOngoingJobsScheduling_(false)
    , HasPendingUnregistration_(false)
    , NodeDescriptor_(nodeDescriptor)
    , DefaultAddress_(NodeDescriptor_.GetDefaultAddress())
{ }

const Stroka& TExecNode::GetDefaultAddress() const
{
    TReaderGuard guard(SpinLock_);
    return DefaultAddress_;
}

bool TExecNode::CanSchedule(const TNullable<Stroka>& tag) const
{
    return !tag || Tags_.find(*tag) != Tags_.end();
}

TExecNodeDescriptor TExecNode::BuildExecDescriptor() const
{
    TReaderGuard guard(SpinLock_);
    return TExecNodeDescriptor{
        Id_,
        GetDefaultAddress(),
        IOWeight_,
        ResourceLimits_
    };
}

double TExecNode::GetIOWeight() const
{
    return IOWeight_;
}

void TExecNode::SetIOWeight(double value)
{
    TWriterGuard guard(SpinLock_);
    IOWeight_ = value;
}

const TJobResources& TExecNode::GetResourceLimits() const
{
    return ResourceLimits_;
}

void TExecNode::SetResourceLimits(const TJobResources& value)
{
    TWriterGuard guard(SpinLock_);
    ResourceLimits_ = value;
}

const TJobResources& TExecNode::GetResourceUsage() const
{
    return ResourceUsage_;
}

void TExecNode::SetResourceUsage(const TJobResources& value)
{
    // NB: No locking is needed since ResourceUsage_ is not used
    // in BuildExecDescriptor.
    ResourceUsage_ = value;
}

TNodeDescriptor TExecNode::GetNodeDescriptor() const
{
    TReaderGuard guard(SpinLock_);
    return NodeDescriptor_;
}

void TExecNode::SetNodeDescriptor(const NNodeTrackerClient::TNodeDescriptor& descriptor)
{
    TWriterGuard guard(SpinLock_);
    YCHECK(DefaultAddress_ == descriptor.GetDefaultAddress());
    NodeDescriptor_ = descriptor;
}

////////////////////////////////////////////////////////////////////

TExecNodeDescriptor::TExecNodeDescriptor()
{ }

TExecNodeDescriptor::TExecNodeDescriptor(
    NNodeTrackerClient::TNodeId id,
    Stroka address,
    double ioWeight,
    TJobResources resourceLimits)
    : Id(id)
    , Address(address)
    , IOWeight(ioWeight)
    , ResourceLimits(resourceLimits)
{ }

void TExecNodeDescriptor::Persist(TStreamPersistenceContext& context)
{
    using NYT::Persist;

    Persist(context, Id);
    Persist(context, Address);
    Persist(context, IOWeight);
    Persist(context, ResourceLimits);
}

////////////////////////////////////////////////////////////////////

} // namespace NScheduler
} // namespace NYT

