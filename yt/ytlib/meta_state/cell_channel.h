#pragma once

#include "../election/leader_lookup.h"
#include "../rpc/channel.h"

namespace NYT {
namespace NMetaState {

////////////////////////////////////////////////////////////////////////////////

//! Creates a channel that takes care of choosing leader among the peers.
NRpc::IChannel::TPtr CreateCellChannel(const NElection::TLeaderLookup::TConfig& config);

////////////////////////////////////////////////////////////////////////////////

} // namespace NMetaState
} // namespace NYT
