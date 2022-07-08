#pragma once

#include "public.h"

#include <yt/yt/client/chunk_client/public.h>

#include <yt/yt/client/object_client/public.h>

#include <yt/yt/client/security_client/public.h>

#include <yt/yt/client/tablet_client/public.h>

#include <yt/yt/client/hive/public.h>

#include <yt/yt/core/actions/callback.h>

#include <yt/yt/core/rpc/authentication_identity.h>

namespace NYT::NApi {

////////////////////////////////////////////////////////////////////////////////

struct TConnectionOptions
{
    //! If non-null, suppresses creation of a per-connection thread pool.
    IInvokerPtr ConnectionInvoker;
};

////////////////////////////////////////////////////////////////////////////////

struct TClientOptions
{
    static TClientOptions FromUser(const TString& user, const std::optional<TString>& userTag = {});
    static TClientOptions FromAuthenticationIdentity(const NRpc::TAuthenticationIdentity& identity);
    static TClientOptions FromToken(const TString& token);
    static TClientOptions FromServiceTicket(const TString& ticket);

    const TString& GetAuthenticatedUser() const;
    NRpc::TAuthenticationIdentity GetAuthenticationIdentity() const;

    //! This field is not required for authentication.
    //! When not specified, user is derived from credentials. When
    //! specified, server additionally checks that #User is
    //! matching user derived from credentials.
    std::optional<TString> User;

    //! Provides an additional annotation to differentiate between
    //! various clients that authenticate via the same effective user.
    std::optional<TString> UserTag;

    std::optional<TString> Token;
    std::optional<TString> ServiceTicket;
    std::optional<TString> SessionId;
    std::optional<TString> SslSessionId;
};

struct TTransactionParticipantOptions
{
    TDuration RpcTimeout = TDuration::Seconds(5);
};

////////////////////////////////////////////////////////////////////////////////

//! Represents an established connection with a YT cluster.
/*
 *  IConnection instance caches most of the stuff needed for fast interaction
 *  with the cluster (e.g. connection channels, mount info etc).
 *
 *  Thread affinity: any
 */
struct IConnection
    : public virtual TRefCounted
{
    virtual TClusterTag GetClusterTag() const = 0;
    virtual const TString& GetLoggingTag() const = 0;
    virtual const TString& GetClusterId() const = 0;
    virtual IInvokerPtr GetInvoker() = 0;

    // TODO(gritukan): Fix alien transaction creation for RPC proxy connection
    // and eliminate this method.
    virtual bool IsSameCluster(const IConnectionPtr& other) const = 0;

    virtual IClientPtr CreateClient(const TClientOptions& options = {}) = 0;
    virtual NHiveClient::ITransactionParticipantPtr CreateTransactionParticipant(
        NHiveClient::TCellId cellId,
        const TTransactionParticipantOptions& options = TTransactionParticipantOptions()) = 0;

    virtual void ClearMetadataCaches() = 0;
    virtual void Terminate() = 0;
};

DEFINE_REFCOUNTED_TYPE(IConnection)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NApi

