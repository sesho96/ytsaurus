#pragma once

#include "common.h"

#include "../rpc/client.h"

#include "../misc/config.h"

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

struct TCellConfig
{
    yvector<Stroka> PeerAddresses;
    TPeerId Id;

    TCellConfig()
        : Id(InvalidPeerId)
    { }

    void Read(TJsonObject* json)
    {
        NYT::TryRead(json, L"Id", &Id);
        NYT::TryRead(json, L"PeerAddresses", &PeerAddresses);
    }
};

////////////////////////////////////////////////////////////////////////////////

class TCellManager
    : public TRefCountedBase
{
public:
    typedef TIntrusivePtr<TCellManager> TPtr;
    typedef TCellConfig TConfig;

    TCellManager(const TConfig& config);

    TPeerId GetSelfId() const;
    i32 GetPeerCount() const;
    i32 GetQuorum() const;
    Stroka GetPeerAddress(TPeerId id) const;

    template <class TProxy>
    TAutoPtr<TProxy> GetMasterProxy(TPeerId id) const;

private:
    TConfig Config;
    mutable NRpc::TChannelCache ChannelCache;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT

#define CELL_MANAGER_INL_H_
#include "cell_manager-inl.h"
#undef CELL_MANAGER_INL_H_
