#pragma once

#include <yt/ytlib/admin/proto/admin_service.pb.h>

#include <yt/core/rpc/client.h>

namespace NYT::NAdmin {

////////////////////////////////////////////////////////////////////////////////

class TAdminServiceProxy
    : public NRpc::TProxyBase
{
public:
    DEFINE_RPC_PROXY(TAdminServiceProxy, AdminService);

    DEFINE_RPC_PROXY_METHOD(NProto, Die);

    DEFINE_RPC_PROXY_METHOD(NProto, WriteCoreDump);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NAdmin
