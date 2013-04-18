#pragma once

#include "public.h"

#include <ytlib/rpc/public.h>

#include <server/cell_master/public.h>

namespace NYT {
namespace NChunkServer {

////////////////////////////////////////////////////////////////////////////////

NRpc::IServicePtr CreateJobTrackerService(NCellMaster::TBootstrap* boostrap);

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT
