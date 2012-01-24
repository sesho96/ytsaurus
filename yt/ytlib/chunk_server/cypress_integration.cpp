#include "stdafx.h"
#include "cypress_integration.h"

#include <ytlib/misc/string.h>
#include <ytlib/ytree/virtual.h>
#include <ytlib/ytree/fluent.h>
#include <ytlib/cypress/virtual.h>
#include <ytlib/cypress/node_proxy_detail.h>
#include <ytlib/cypress/cypress_ypath_proxy.h>
#include <ytlib/orchid/cypress_integration.h>

namespace NYT {
namespace NChunkServer {

using namespace NYTree;
using namespace NCypress;
using namespace NMetaState;
using namespace NOrchid;
using namespace NObjectServer;

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

    TVirtualChunkMap(TChunkManager* chunkManager, EChunkFilter filter)
        : ChunkManager(chunkManager)
        , Filter(filter)
    { }

private:
    TChunkManager::TPtr ChunkManager;
    EChunkFilter Filter;

    const yhash_set<TChunkId>& GetFilteredChunkIds() const
    {
        switch (Filter) {
            case EChunkFilter::Lost:
                return ChunkManager->LostChunkIds();
            case EChunkFilter::Overreplicated:
                return ChunkManager->OverreplicatedChunkIds();
            case EChunkFilter::Underreplicated:
                return ChunkManager->UnderreplicatedChunkIds();
            default:
                YUNREACHABLE();
        }
    }

    bool CheckFilter(const TChunkId& chunkId) const
    {
        if (Filter == EChunkFilter::All)
            return true;

        const auto& chunkIds = GetFilteredChunkIds();
        return chunkIds.find(chunkId) != chunkIds.end();
    }

    virtual yvector<Stroka> GetKeys(size_t sizeLimit) const
    {
        if (Filter == EChunkFilter::All) {
            const auto& chunkIds = ChunkManager->GetChunkIds(sizeLimit);
            return ConvertToStrings(chunkIds.begin(), Min(chunkIds.size(), sizeLimit));
        } else {
            const auto& chunkIds = GetFilteredChunkIds();
            return ConvertToStrings(chunkIds.begin(), Min(chunkIds.size(), sizeLimit));
        }
    }

    virtual size_t GetSize() const
    {
        if (Filter == EChunkFilter::All) {
            return ChunkManager->GetChunkCount();
        } else {
            return GetFilteredChunkIds().size();
        }
    }

    virtual IYPathService::TPtr GetItemService(const Stroka& key) const
    {
        auto id = TChunkId::FromString(key);

        if (!CheckFilter(id)) {
            return NULL;
        }

        return ChunkManager->GetObjectManager()->FindProxy(id);

        //return IYPathService::FromProducer(~FromFunctor([=] (IYsonConsumer* consumer)
        //    {
        //        BuildYsonFluently(consumer)
        //            .BeginMap()
        //            .EndMap();
            //}));
    }
};

INodeTypeHandler::TPtr CreateChunkMapTypeHandler(
    TCypressManager* cypressManager,
    TChunkManager* chunkManager)
{
    YASSERT(cypressManager);
    YASSERT(chunkManager);

    return CreateVirtualTypeHandler(
        cypressManager,
        EObjectType::ChunkMap,
        ~New<TVirtualChunkMap>(chunkManager, TVirtualChunkMap::EChunkFilter::All));
}

INodeTypeHandler::TPtr CreateLostChunkMapTypeHandler(
    TCypressManager* cypressManager,
    TChunkManager* chunkManager)
{
    YASSERT(cypressManager);
    YASSERT(chunkManager);

    return CreateVirtualTypeHandler(
        cypressManager,
        EObjectType::LostChunkMap,
        ~New<TVirtualChunkMap>(chunkManager, TVirtualChunkMap::EChunkFilter::Lost));
}

INodeTypeHandler::TPtr CreateOverreplicatedChunkMapTypeHandler(
    TCypressManager* cypressManager,
    TChunkManager* chunkManager)
{
    YASSERT(cypressManager);
    YASSERT(chunkManager);

    return CreateVirtualTypeHandler(
        cypressManager,
        EObjectType::OverreplicatedChunkMap,
        ~New<TVirtualChunkMap>(chunkManager, TVirtualChunkMap::EChunkFilter::Overreplicated));
}

INodeTypeHandler::TPtr CreateUnderreplicatedChunkMapTypeHandler(
    TCypressManager* cypressManager,
    TChunkManager* chunkManager)
{
    YASSERT(cypressManager);
    YASSERT(chunkManager);

    return CreateVirtualTypeHandler(
        cypressManager,
        EObjectType::UnderreplicatedChunkMap,
        ~New<TVirtualChunkMap>(chunkManager, TVirtualChunkMap::EChunkFilter::Underreplicated));
}

////////////////////////////////////////////////////////////////////////////////

class TVirtualChunkListMap
    : public TVirtualMapBase
{
public:
    TVirtualChunkListMap(TChunkManager* chunkManager)
        : ChunkManager(chunkManager)
    { }

private:
    TChunkManager::TPtr ChunkManager;

    virtual yvector<Stroka> GetKeys(size_t sizeLimit) const
    {
        const auto& chunkListIds = ChunkManager->GetChunkListIds(sizeLimit);
        return ConvertToStrings(chunkListIds.begin(), Min(chunkListIds.size(), sizeLimit));
    }

    virtual size_t GetSize() const
    {
        return ChunkManager->GetChunkListCount();
    }

    virtual IYPathService::TPtr GetItemService(const Stroka& key) const
    {
        auto id = TChunkListId::FromString(key);
        return ChunkManager->GetObjectManager()->GetProxy(id);
    }
};

INodeTypeHandler::TPtr CreateChunkListMapTypeHandler(
    TCypressManager* cypressManager,
    TChunkManager* chunkManager)
{
    YASSERT(cypressManager);
    YASSERT(chunkManager);

    return CreateVirtualTypeHandler(
        cypressManager,
        EObjectType::ChunkListMap,
        ~New<TVirtualChunkListMap>(chunkManager));
}

////////////////////////////////////////////////////////////////////////////////

class THolderAuthority
    : public IHolderAuthority
{
public:
    typedef TIntrusivePtr<THolderAuthority> TPtr;

    THolderAuthority(TCypressManager* cypressManager)
        : CypressManager(cypressManager)
    { }

    virtual bool IsHolderAuthorized(const Stroka& address)
    {
        UNUSED(address);
        return true;
    }
    
private:
    TCypressManager::TPtr CypressManager;

};

IHolderAuthority::TPtr CreateHolderAuthority(
    TCypressManager* cypressManager)
{
    return New<THolderAuthority>(cypressManager);
}

////////////////////////////////////////////////////////////////////////////////

class THolderTypeHandler
    : public TMapNodeTypeHandler
{
public:
    typedef THolderTypeHandler TThis;
    typedef TIntrusivePtr<TThis> TPtr;

    THolderTypeHandler(
        TCypressManager* cypressManager,
        TChunkManager* chunkManager)
        : TMapNodeTypeHandler(cypressManager)
        , CypressManager(cypressManager)
        , ChunkManager(chunkManager)
    {
        // NB: No smartpointer for this here.
        RegisterGetter("alive", FromMethod(&TThis::GetAlive, this));
        RegisterGetter("statistics", FromMethod(&TThis::GetStatistics, this));
    }

    virtual EObjectType GetObjectType()
    {
        return EObjectType::Holder;
    }

    virtual TAutoPtr<ICypressNode> CreateFromManifest(
        const TNodeId& nodeId,
        const TTransactionId& transactionId,
        NYTree::IMapNode* manifest)
    {
        UNUSED(transactionId);
        UNUSED(manifest);
        return Create(nodeId);
    }

private:
    TCypressManager::TPtr CypressManager;
    TChunkManager::TPtr ChunkManager;

    Stroka GetAddress(const ICypressNode& node)
    {
        auto proxy = CypressManager->GetNodeProxy(node.GetId().ObjectId, NullTransactionId);
        return proxy->GetParent()->AsMap()->GetChildKey(~proxy);
    }

    void GetAlive(const TGetAttributeParam& param)
    {
        auto address = GetAddress(*param.Node);
        bool alive = ChunkManager->FindHolder(address);
        BuildYsonFluently(param.Consumer)
            .Scalar(alive);
    }

    void GetStatistics(const TGetAttributeParam& param)
    {
        auto address = GetAddress(*param.Node);
        auto* holder = ChunkManager->FindHolder(address);
        BuildYsonFluently(param.Consumer)
            .BeginMap()
                .DoIf(holder, [=] (TFluentMap fluent)
                    {
                        const auto statistics = holder->Statistics();
                        fluent
                            .Item("available_space").Scalar(statistics.available_space())
                            .Item("used_space").Scalar(statistics.used_space())
                            .Item("chunk_count").Scalar(statistics.chunk_count())
                            .Item("session_count").Scalar(statistics.session_count())
                            .Item("full").Scalar(statistics.full());
                    })
            .EndMap();
    }
};

INodeTypeHandler::TPtr CreateHolderTypeHandler(
    TCypressManager* cypressManager,
    TChunkManager* chunkManager)
{
    YASSERT(cypressManager);
    YASSERT(chunkManager);

    return New<THolderTypeHandler>(cypressManager, chunkManager);
}

////////////////////////////////////////////////////////////////////////////////

class THolderMapBehavior
    : public TNodeBehaviorBase<TMapNode, TMapNodeProxy>
{
public:
    typedef TNodeBehaviorBase<TMapNode, TMapNodeProxy> TBase;
    typedef THolderMapBehavior TThis;
    typedef TIntrusivePtr<TThis> TPtr;

    THolderMapBehavior(
        const ICypressNode& node,
        IMetaStateManager* metaStateManager,
        TCypressManager* cypressManager,
        TChunkManager* chunkManager)
        : TBase(node, cypressManager)
        , ChunkManager(chunkManager)
    {
        OnRegistered_ =
            FromMethod(&TThis::OnRegistered, TPtr(this))
            ->Via(metaStateManager->GetEpochStateInvoker());
        ChunkManager->HolderRegistered().Subscribe(OnRegistered_);
    }

    virtual void Destroy()
    {
        ChunkManager->HolderRegistered().Unsubscribe(OnRegistered_);
        OnRegistered_.Reset();
    }

private:
    TChunkManager::TPtr ChunkManager;

    IParamAction<const THolder&>::TPtr OnRegistered_;
    
    void OnRegistered(const THolder& holder)
    {
        Stroka address = holder.GetAddress();

        auto node = GetProxy();
        if (node->FindChild(address))
            return;


        auto processor = CypressManager->CreateProcessor();

        // TODO(babenko): make a single transaction

        {
            auto req = TCypressYPathProxy::Create(CombineYPaths(
                FromObjectId(NodeId),
                address));
            req->set_type(EObjectType::Holder);
            ExecuteVerb(~req, ~processor);
        }

        {
            auto req = TCypressYPathProxy::Create(CombineYPaths(
                FromObjectId(NodeId),
                address,
                "orchid"));
            req->set_type(EObjectType::Orchid);     
            auto manifest = New<TOrchidManifest>();
            manifest->RemoteAddress = address;
            req->set_manifest(SerializeToYson(~manifest));
            ExecuteVerb(~req, ~processor);
        }
    }

};

class THolderMapTypeHandler
    : public TMapNodeTypeHandler
{
public:
    typedef THolderMapTypeHandler TThis;
    typedef TIntrusivePtr<TThis> TPtr;

    THolderMapTypeHandler(
        IMetaStateManager* metaStateManager,
        TCypressManager* cypressManager,
        TChunkManager* chunkManager)
        : TMapNodeTypeHandler(cypressManager)
        , MetaStateManager(metaStateManager)
        , CypressManager(cypressManager)
        , ChunkManager(chunkManager)
    {
        // NB: No smartpointer for this here.
        RegisterGetter("alive", FromMethod(&TThis::GetAliveHolders, this));
        RegisterGetter("dead", FromMethod(&TThis::GetDeadHolders, this));
    }

    virtual EObjectType GetObjectType()
    {
        return EObjectType::HolderMap;
    }

    virtual TAutoPtr<ICypressNode> CreateFromManifest(
        const TNodeId& nodeId,
        const TTransactionId& transactionId,
        NYTree::IMapNode* manifest)
    {
        UNUSED(transactionId);
        UNUSED(manifest);
        return Create(nodeId);
    }

    virtual INodeBehavior::TPtr CreateBehavior(const ICypressNode& node)
    {
        return New<THolderMapBehavior>(node, ~MetaStateManager, ~CypressManager, ~ChunkManager);
    }

private:
    IMetaStateManager::TPtr MetaStateManager;
    TCypressManager::TPtr CypressManager;
    TChunkManager::TPtr ChunkManager;

    void GetAliveHolders(const TGetAttributeParam& param)
    {
        BuildYsonFluently(param.Consumer)
            .DoListFor(ChunkManager->GetHolderIds(), [=] (TFluentList fluent, THolderId id)
                {
                    const auto& holder = ChunkManager->GetHolder(id);
                    fluent.Item().Scalar(holder.GetAddress());
                });
    }

    void GetDeadHolders(const TGetAttributeParam& param)
    {
        BuildYsonFluently(param.Consumer)
            .DoListFor(param.Node->NameToChild(), [=] (TFluentList fluent, TPair<Stroka, TNodeId> pair)
                {
                    Stroka address = pair.first;
                    if (!ChunkManager->FindHolder(address)) {
                        param.Consumer->OnListItem();
                        param.Consumer->OnStringScalar(address);
                    }
                });
    }
};

INodeTypeHandler::TPtr CreateHolderMapTypeHandler(
    IMetaStateManager* metaStateManager,
    TCypressManager* cypressManager,
    TChunkManager* chunkManager)
{
    YASSERT(cypressManager);
    YASSERT(chunkManager);

    return New<THolderMapTypeHandler>(
        metaStateManager,
        cypressManager,
        chunkManager);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT
