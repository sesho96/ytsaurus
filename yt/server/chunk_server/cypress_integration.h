#pragma once

#include "public.h"

#include <server/cypress_server/public.h>
#include <server/cell_master/public.h>

namespace NYT {
namespace NChunkServer {

////////////////////////////////////////////////////////////////////////////////

NCypressServer::INodeTypeHandlerPtr CreateChunkMapTypeHandler(NCellMaster::TBootstrap* bootstrap, NObjectClient::EObjectType type);

NCypressServer::INodeTypeHandlerPtr CreateChunkListMapTypeHandler(NCellMaster::TBootstrap* bootstrap);

INodeAuthorityPtr CreateNodeAuthority(NCellMaster::TBootstrap* bootstrap);

NCypressServer::INodeTypeHandlerPtr CreateNodeTypeHandler(NCellMaster::TBootstrap* bootstrap);

NCypressServer::INodeTypeHandlerPtr CreateNodeMapTypeHandler(NCellMaster::TBootstrap* bootstrap);

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT
