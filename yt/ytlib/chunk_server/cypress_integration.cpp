#include "stdafx.h"
#include "cypress_integration.h"
#include "holder.h"
#include "holder_statistics.h"

#include <ytlib/actions/bind.h>
#include <ytlib/misc/string.h>
#include <ytlib/ytree/virtual.h>
#include <ytlib/ytree/fluent.h>
#include <ytlib/cypress_server/virtual.h>
#include <ytlib/cypress_server/node_proxy_detail.h>
#include <ytlib/cypress_client/cypress_ypath_proxy.h>
#include <ytlib/chunk_server/chunk_manager.h>
#include <ytlib/chunk_server/holder_authority.h>
#include <ytlib/orchid/cypress_integration.h>
#include <ytlib/cell_master/bootstrap.h>

namespace NYT {
namespace NChunkServer {

using namespace NYTree;
using namespace NCypressServer;
using namespace NCypressClient;
using namespace NMetaState;
using namespace NOrchid;
using namespace NObjectServer;
using namespace NTransactionServer;
using namespace NCellMaster;

////////////////////////////////////////////////////////////////////////////////

class TVirtualChunkMap
    : public TVirtualMapBase
{
public:
    DECLARE_ENUM(EChunkFilter,
        (All)
        (Lost)
        (Overreplicated)
        (Underreplicated)
    );

    TVirtualChunkMap(TBootstrap* bootstrap, EChunkFilter filter)
        : Bootstrap(bootstrap)
        , Filter(filter)
    { }

private:
    TBootstrap* Bootstrap;
    EChunkFilter Filter;

    const yhash_set<TChunkId>& GetFilteredChunkIds() const
    {
        auto chunkManager = Bootstrap->GetChunkManager();
        switch (Filter) {
            case EChunkFilter::Lost:
                return chunkManager->LostChunkIds();
            case EChunkFilter::Overreplicated:
                return chunkManager->OverreplicatedChunkIds();
            case EChunkFilter::Underreplicated:
                return chunkManager->UnderreplicatedChunkIds();
            default:
                YUNREACHABLE();
        }
    }

    bool CheckFilter(const TChunkId& chunkId) const
    {
        if (Filter == EChunkFilter::All) {
            return true;
        }

        const auto& chunkIds = GetFilteredChunkIds();
        return chunkIds.find(chunkId) != chunkIds.end();
    }

    virtual std::vector<Stroka> GetKeys(size_t sizeLimit) const
    {
        if (Filter == EChunkFilter::All) {
            const auto& chunkIds = Bootstrap->GetChunkManager()->GetChunkIds(sizeLimit);
            return ConvertToStrings(chunkIds.begin(), chunkIds.end(), sizeLimit);
        } else {
            const auto& chunkIds = GetFilteredChunkIds();
            return ConvertToStrings(chunkIds.begin(), chunkIds.end(), sizeLimit);
        }
    }

    virtual size_t GetSize() const
    {
        if (Filter == EChunkFilter::All) {
            return Bootstrap->GetChunkManager()->GetChunkCount();
        } else {
            return GetFilteredChunkIds().size();
        }
    }

    virtual IYPathServicePtr GetItemService(const TStringBuf& key) const
    {
        auto id = TChunkId::FromString(key);

        if (TypeFromId(id) != EObjectType::Chunk) {
            return NULL;
        }

        if (!CheckFilter(id)) {
            return NULL;
        }

        return Bootstrap->GetObjectManager()->FindProxy(id);
    }
};

INodeTypeHandlerPtr CreateChunkMapTypeHandler(TBootstrap* bootstrap)
{
    YASSERT(bootstrap);

    return CreateVirtualTypeHandler(
        bootstrap,
        EObjectType::ChunkMap,
        New<TVirtualChunkMap>(bootstrap, TVirtualChunkMap::EChunkFilter::All));
}

INodeTypeHandlerPtr CreateLostChunkMapTypeHandler(TBootstrap* bootstrap)
{
    YASSERT(bootstrap);

    return CreateVirtualTypeHandler(
        bootstrap,
        EObjectType::LostChunkMap,
        New<TVirtualChunkMap>(bootstrap, TVirtualChunkMap::EChunkFilter::Lost));
}

INodeTypeHandlerPtr CreateOverreplicatedChunkMapTypeHandler(TBootstrap* bootstrap)
{
    YASSERT(bootstrap);

    return CreateVirtualTypeHandler(
        bootstrap,
        EObjectType::OverreplicatedChunkMap,
        New<TVirtualChunkMap>(bootstrap, TVirtualChunkMap::EChunkFilter::Overreplicated));
}

INodeTypeHandlerPtr CreateUnderreplicatedChunkMapTypeHandler(TBootstrap* bootstrap)
{
    YASSERT(bootstrap);

    return CreateVirtualTypeHandler(
        bootstrap,
        EObjectType::UnderreplicatedChunkMap,
        New<TVirtualChunkMap>(bootstrap, TVirtualChunkMap::EChunkFilter::Underreplicated));
}

////////////////////////////////////////////////////////////////////////////////

class TVirtualChunkListMap
    : public TVirtualMapBase
{
public:
    TVirtualChunkListMap(TBootstrap* bootstrap)
        : Bootstrap(bootstrap)
    { }

private:
    TBootstrap* Bootstrap;

    virtual std::vector<Stroka> GetKeys(size_t sizeLimit) const
    {
        const auto& chunkListIds = Bootstrap->GetChunkManager()->GetChunkListIds(sizeLimit);
        return ConvertToStrings(chunkListIds.begin(), chunkListIds.end(), sizeLimit);
    }

    virtual size_t GetSize() const
    {
        return Bootstrap->GetChunkManager()->GetChunkListCount();
    }

    virtual IYPathServicePtr GetItemService(const TStringBuf& key) const
    {
        auto id = TChunkListId::FromString(key);
        if (TypeFromId(id) != EObjectType::ChunkList) {
            return NULL;
        }
        return Bootstrap->GetObjectManager()->FindProxy(id);
    }
};

INodeTypeHandlerPtr CreateChunkListMapTypeHandler(TBootstrap* bootstrap)
{
    YASSERT(bootstrap);

    return CreateVirtualTypeHandler(
        bootstrap,
        EObjectType::ChunkListMap,
        New<TVirtualChunkListMap>(bootstrap));
}

////////////////////////////////////////////////////////////////////////////////

class TNodeAuthority
    : public INodeAuthority
{
public:
    typedef TIntrusivePtr<TNodeAuthority> TPtr;

    TNodeAuthority(TBootstrap* bootstrap)
        : Bootstrap(bootstrap)
    { }

    virtual bool IsAuthorized(const Stroka& address)
    {
        auto cypressManager = Bootstrap->GetCypressManager();
        auto resolver = cypressManager->CreateResolver();
        auto nodeMap = resolver->ResolvePath("//sys/holders")->AsMap();
        auto node = nodeMap->FindChild(address);

        if (!node) {
            // New node.
            return true;
        }

        bool banned = node->Attributes().Get<bool>("banned", false);
        return !banned;
    }
    
private:
    TBootstrap* Bootstrap;

};

INodeAuthorityPtr CreateNodeAuthority(TBootstrap* bootstrap)
{
    return New<TNodeAuthority>(bootstrap);
}

////////////////////////////////////////////////////////////////////////////////

class TNodeProxy
    : public TMapNodeProxy
{
public:
    TNodeProxy(
        INodeTypeHandlerPtr typeHandler,
        TBootstrap* bootstrap,
        NTransactionServer::TTransaction* transaction,
        const NCypressServer::TNodeId& nodeId)
        : TMapNodeProxy(
            typeHandler,
            bootstrap,
            transaction,
            nodeId)
    { }

private:
    THolder* GetNode() const
    {
        auto address = GetParent()->AsMap()->GetChildKey(this);
        return Bootstrap->GetChunkManager()->FindNodeByAddress(address);
    }

    virtual void GetSystemAttributes(std::vector<TAttributeInfo>* attributes)
    {
        const auto* node = GetNode();
        attributes->push_back(TAttributeInfo("state"));
        attributes->push_back(TAttributeInfo("confirmed", node));
        attributes->push_back(TAttributeInfo("incarnation_id", node));
        attributes->push_back(TAttributeInfo("available_space", node));
        attributes->push_back(TAttributeInfo("used_space", node));
        attributes->push_back(TAttributeInfo("chunk_count", node));
        attributes->push_back(TAttributeInfo("session_count", node));
        attributes->push_back(TAttributeInfo("full", node));
        TMapNodeProxy::GetSystemAttributes(attributes);
    }

    virtual bool GetSystemAttribute(const Stroka& name, NYTree::IYsonConsumer* consumer)
    {
        const auto* node = GetNode();

        if (name == "state") {
            auto state = node ? node->GetState() : ENodeState(ENodeState::Offline);
            BuildYsonFluently(consumer)
                .Scalar(FormatEnum(state));
            return true;
        }

        if (node) {
            if (name == "confirmed") {
                BuildYsonFluently(consumer)
                    .Scalar(FormatBool(Bootstrap->GetChunkManager()->IsNodeConfirmed(node)));
                return true;
            }

            if (name == "incarnation_id") {
                BuildYsonFluently(consumer)
                    .Scalar(node->GetIncarnationId());
                return true;
            }

            const auto& statistics = node->Statistics();
            if (name == "available_space") {
                BuildYsonFluently(consumer)
                    .Scalar(statistics.available_space());
                return true;
            }
            if (name == "used_space") {
                BuildYsonFluently(consumer)
                    .Scalar(statistics.used_space());
                return true;
            }
            if (name == "chunk_count") {
                BuildYsonFluently(consumer)
                    .Scalar(statistics.chunk_count());
                return true;
            }
            if (name == "session_count") {
                BuildYsonFluently(consumer)
                    .Scalar(statistics.session_count());
                return true;
            }
            if (name == "full") {
                BuildYsonFluently(consumer)
                    .Scalar(statistics.full());
                return true;
            }
        }

        return TMapNodeProxy::GetSystemAttribute(name, consumer);
    }

    virtual void OnUpdateAttribute(
        const Stroka& key,
        const TNullable<NYTree::TYsonString>& oldValue,
        const TNullable<NYTree::TYsonString>& newValue)
    {
        UNUSED(oldValue);
        if (key == "banned") {
            if (newValue) {
                ConvertTo<bool>(*newValue);
            }
        }
    }
};

class TNodeTypeHandler
    : public TMapNodeTypeHandler
{
public:
    explicit TNodeTypeHandler(TBootstrap* bootstrap)
        : TMapNodeTypeHandler(bootstrap)
    { }

    virtual EObjectType GetObjectType()
    {
        return EObjectType::Node;
    }

    virtual ICypressNodeProxyPtr GetProxy(
        const NCypressServer::TNodeId& nodeId,
        TTransaction* transaction)
    {
        return New<TNodeProxy>(
            this,
            Bootstrap,
            transaction,
            nodeId);
    }
};

INodeTypeHandlerPtr CreateNodeTypeHandler(TBootstrap* bootstrap)
{
    YASSERT(bootstrap);

    return New<TNodeTypeHandler>(bootstrap);
}

////////////////////////////////////////////////////////////////////////////////

class TNodeMapBehavior
    : public TNodeBehaviorBase<TMapNode, TMapNodeProxy>
{
public:
    TNodeMapBehavior(TBootstrap* bootstrap, const NCypressServer::TNodeId& nodeId)
        : TNodeBehaviorBase<TMapNode, TMapNodeProxy>(bootstrap, nodeId)
    {
        bootstrap->GetChunkManager()->SubscribeNodeRegistered(BIND(
            &TNodeMapBehavior::OnRegistered,
            MakeWeak(this)));
    }

private:
    void OnRegistered(const THolder* node)
    {
        Stroka address = node->GetAddress();
        auto proxy = GetProxy();

        auto cypressManager = Bootstrap->GetCypressManager();

        // We're already in the state thread but need to postpone the planned changes and enqueue a callback.
        // Doing otherwise will turn node registration and Cypress update into a single
        // logged change, which is undesirable.
        BIND([=] () {
            if (proxy->FindChild(address))
                return;

            auto service = cypressManager->GetVersionedNodeProxy(NodeId);

            // TODO(babenko): make a single transaction
            // TODO(babenko): check for errors and retry

            {
                auto req = TCypressYPathProxy::Create("/" + EscapeYPathToken(address));
                req->set_type(EObjectType::Node);
                ExecuteVerb(service, req);
            }

            {
                auto req = TCypressYPathProxy::Create("/" + EscapeYPathToken(address) + "/orchid");
                req->set_type(EObjectType::Orchid);
                req->Attributes().Set<Stroka>("remote_address", address);
                ExecuteVerb(service, req);
            }
        })
        .Via(
            Bootstrap->GetStateInvoker(),
            Bootstrap->GetMetaStateManager()->GetEpochContext())
        .Run();
    }

};

class THolderMapProxy
    : public TMapNodeProxy
{
public:
    THolderMapProxy(
        INodeTypeHandlerPtr typeHandler,
        TBootstrap* bootstrap,
        NTransactionServer::TTransaction* transaction,
        const NCypressServer::TNodeId& nodeId)
        : TMapNodeProxy(
            typeHandler,
            bootstrap,
            transaction,
            nodeId)
    { }

private:
    virtual void GetSystemAttributes(std::vector<TAttributeInfo>* attributes)
    {
        attributes->push_back("offline");
        attributes->push_back("registered");
        attributes->push_back("online");
        attributes->push_back("unconfirmed");
        attributes->push_back("confirmed");
        attributes->push_back("available_space");
        attributes->push_back("used_space");
        attributes->push_back("chunk_count");
        attributes->push_back("session_count");
        attributes->push_back("online_holder_count");
        attributes->push_back("chunk_replicator_enabled");
        TMapNodeProxy::GetSystemAttributes(attributes);
    }

    virtual bool GetSystemAttribute(const Stroka& name, NYTree::IYsonConsumer* consumer)
    {
        auto chunkManager = Bootstrap->GetChunkManager();

        if (name == "offline") {
            BuildYsonFluently(consumer)
                .DoListFor(GetKeys(), [=] (TFluentList fluent, Stroka address) {
                    if (!chunkManager->FindNodeByAddress(address)) {
                        fluent.Item().Scalar(address);
                    }
            });
            return true;
        }

        if (name == "registered" || name == "online") {
            auto state = name == "registered" ? ENodeState::Registered : ENodeState::Online;
            BuildYsonFluently(consumer)
                .DoListFor(chunkManager->GetNodes(), [=] (TFluentList fluent, THolder* holder) {
                    if (holder->GetState() == state) {
                        fluent.Item().Scalar(holder->GetAddress());
                    }
                });
            return true;
        }

        if (name == "unconfirmed" || name == "confirmed") {
            bool state = name == "confirmed";
            BuildYsonFluently(consumer)
                .DoListFor(chunkManager->GetNodes(), [=] (TFluentList fluent, THolder* holder) {
                    if (chunkManager->IsNodeConfirmed(holder) == state) {
                        fluent.Item().Scalar(holder->GetAddress());
                    }
                });
            return true;
        }

        auto statistics = chunkManager->GetTotalNodeStatistics();
        if (name == "available_space") {
            BuildYsonFluently(consumer)
                .Scalar(statistics.AvailbaleSpace);
            return true;
        }

        if (name == "used_space") {
            BuildYsonFluently(consumer)
                .Scalar(statistics.UsedSpace);
            return true;
        }

        if (name == "chunk_count") {
            BuildYsonFluently(consumer)
                .Scalar(statistics.ChunkCount);
            return true;
        }

        if (name == "session_count") {
            BuildYsonFluently(consumer)
                .Scalar(statistics.SessionCount);
            return true;
        }

        if (name == "online_holder_count") {
            BuildYsonFluently(consumer)
                .Scalar(statistics.OnlineNodeCount);
            return true;
        }

        if (name == "chunk_replicator_enabled") {
            BuildYsonFluently(consumer)
                .Scalar(chunkManager->IsReplicatorEnabled());
            return true;
        }

        return TMapNodeProxy::GetSystemAttribute(name, consumer);
    }
};

class THolderMapTypeHandler
    : public TMapNodeTypeHandler
{
public:
    explicit THolderMapTypeHandler(TBootstrap* bootstrap)
        : TMapNodeTypeHandler(bootstrap)
    { }

    virtual EObjectType GetObjectType()
    {
        return EObjectType::NodeMap;
    }
    
    virtual ICypressNodeProxyPtr GetProxy(
        const NCypressServer::TNodeId& nodeId,
        TTransaction* transaction)
    {
        return New<THolderMapProxy>(
            this,
            Bootstrap,
            transaction,
            nodeId);
    }

    virtual INodeBehaviorPtr CreateBehavior(const NCypressServer::TNodeId& nodeId)
    {
        return New<TNodeMapBehavior>(Bootstrap, nodeId);
    }
};

INodeTypeHandlerPtr CreateNodeMapTypeHandler(TBootstrap* bootstrap)
{
    YASSERT(bootstrap);

    return New<THolderMapTypeHandler>(bootstrap);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT
