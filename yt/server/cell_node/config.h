#pragma once

#include "public.h"

#include <ytlib/rpc/config.h>

#include <server/misc/config.h>

#include <server/exec_agent/config.h>

namespace NYT {
namespace NCellNode {

////////////////////////////////////////////////////////////////////////////////

class TCellNodeConfig
    : public TServerConfig
{
public:
    //! RPC interface port number.
    int RpcPort;

    //! HTTP monitoring interface port number.
    int MonitoringPort;

    //! Cell masters.
    NMetaState::TMasterDiscoveryConfigPtr Masters;

    //! Data node configuration part.
    NChunkHolder::TDataNodeConfigPtr DataNode;

    //! Exec node configuration part.
    NExecAgent::TExecAgentConfigPtr ExecAgent;

    //! Throttling configuration for jobs-to-master communication.
    NRpc::TThrottlingChannelConfigPtr JobsToMasterChannel;

    TCellNodeConfig()
    {
        RegisterParameter("rpc_port", RpcPort)
            .Default(9000);
        RegisterParameter("monitoring_port", MonitoringPort)
            .Default(10000);
        RegisterParameter("masters", Masters)
            .DefaultNew();
        RegisterParameter("data_node", DataNode)
            .DefaultNew();
        RegisterParameter("exec_agent", ExecAgent)
            .DefaultNew();
        RegisterParameter("jobs_to_master_channel", JobsToMasterChannel)
            .DefaultNew();

        SetKeepOptions(true);
    }
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NCellNode
} // namespace NYT
