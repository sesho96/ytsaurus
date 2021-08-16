#include "account_proxy.h"
#include "account.h"
#include "helpers.h"
#include "security_manager.h"

#include <yt/yt/server/master/cell_master/bootstrap.h>

#include <yt/yt/server/lib/misc/interned_attributes.h>

#include <yt/yt/server/master/object_server/helpers.h>
#include <yt/yt/server/master/object_server/map_object_proxy.h>
#include <yt/yt/server/master/object_server/object_detail.h>

#include <yt/yt/server/master/chunk_server/chunk_manager.h>
#include <yt/yt/server/master/chunk_server/medium.h>

#include <yt/yt/ytlib/security_client/proto/account_ypath.pb.h>

#include <yt/yt/ytlib/object_client/config.h>

#include <yt/yt/core/yson/async_writer.h>

#include <yt/yt/core/ytree/fluent.h>
#include <yt/yt/core/ytree/node_detail.h>
#include <yt/yt/core/ytree/virtual.h>

namespace NYT::NSecurityServer {

using namespace NChunkServer;
using namespace NObjectServer;
using namespace NRpc;
using namespace NYson;
using namespace NYTree;

using ::ToString;

////////////////////////////////////////////////////////////////////////////////

class TAccountProxy
    : public TNonversionedMapObjectProxyBase<TAccount>
{
private:
    using TBase = TNonversionedMapObjectProxyBase<TAccount>;
    using TBasePtr = TIntrusivePtr<TBase>;

public:
    TAccountProxy(
        NCellMaster::TBootstrap* bootstrap,
        TObjectTypeMetadata* metadata,
        TAccount* account)
        : TBase(bootstrap, metadata, account)
    { }

protected:
    virtual std::unique_ptr<TNonversionedMapObjectFactoryBase<TAccount>> CreateObjectFactory() const override
    {
        return std::make_unique<TAccountFactory>(Bootstrap_);
    }

    virtual TBasePtr ResolveNameOrThrow(const TString& name) override
    {
        const auto& securityManager = Bootstrap_->GetSecurityManager();
        auto* account = securityManager->GetAccountByNameOrThrow(name, true /*activeLifeStageOnly*/);
        return GetProxy(account);
    }

    virtual void ValidateBeforeAttachChild(
        const TString& key,
        const TIntrusivePtr<TNonversionedMapObjectProxyBase<TAccount>>& child) override
    {
        TBase::ValidateBeforeAttachChild(key, child);

        auto* childAccount = child->GetObject()->As<TAccount>();
        auto* impl = GetThisImpl();

        const auto& securityManager = Bootstrap_->GetSecurityManager();
        securityManager->ValidateAttachChildAccount(impl, childAccount);
    }

private:
    class TAccountFactory
        : public TNonversionedMapObjectFactoryBase<TAccount>
    {
    public:
        explicit TAccountFactory(NCellMaster::TBootstrap* bootstrap)
            : TNonversionedMapObjectFactoryBase<TAccount>(bootstrap)
        { }

    protected:
        virtual TAccount* DoCreateObject(IAttributeDictionary* attributes) override
        {
            const auto& securityManager = Bootstrap_->GetSecurityManager();
            auto hintId = attributes->GetAndRemove("hint_id", NullObjectId);
            return securityManager->CreateAccount(hintId);
        }
    };

    virtual void ValidateRemoval() override
    {
        const auto* account = GetThisImpl();
        if (account->IsBuiltin()) {
            THROW_ERROR_EXCEPTION("Cannot remove a built-in account %Qv",
                account->GetName());
        }

        TBase::ValidateRemoval();
    }

    virtual void ValidateChildNameAvailability(const TString& childName) override
    {
        TNonversionedMapObjectProxyBase::ValidateChildNameAvailability(childName);

        if (Bootstrap_->GetSecurityManager()->FindAccountByName(childName, false /*activeLifeStageOnly*/)) {
            THROW_ERROR_EXCEPTION(
                NYTree::EErrorCode::AlreadyExists,
                "Account %Qv already exists",
                childName);
        }
    }

    bool IsRootAccount() const
    {
        const auto& securityManager = Bootstrap_->GetSecurityManager();
        return GetThisImpl() == securityManager->GetRootAccount();
    }

    virtual void ListSystemAttributes(std::vector<TAttributeDescriptor>* descriptors) override
    {
        const auto* account = GetThisImpl();
        TBase::ListSystemAttributes(descriptors);

        auto isRootAccount = IsRootAccount();
        descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::ResourceUsage)
            .SetPresent(!isRootAccount));
        descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::CommittedResourceUsage)
            .SetPresent(!isRootAccount));
        descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::MulticellStatistics)
            .SetOpaque(true)
            .SetPresent(!isRootAccount));

        descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::AllowChildrenLimitOvercommit)
            .SetWritable(true)
            .SetReplicated(true)
            .SetMandatory(true)
            .SetPresent(!isRootAccount));
        descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::ResourceLimits)
            .SetWritable(true)
            .SetReplicated(true)
            .SetMandatory(true)
            .SetPresent(!isRootAccount));
        descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::ViolatedResourceLimits)
            .SetPresent(!isRootAccount));

        descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::RecursiveResourceUsage));
        descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::RecursiveCommittedResourceUsage));
        descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::RecursiveViolatedResourceLimits)
            .SetOpaque(true));
        descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::TotalChildrenResourceLimits)
            .SetOpaque(true));
        descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::MergeJobRateLimit)
            .SetWritable(true)
            .SetWritePermission(EPermission::Administer));
        descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::Abc)
            .SetWritable(true)
            .SetWritePermission(EPermission::Administer)
            .SetReplicated(true)
            .SetRemovable(true)
            .SetPresent(account->GetAbcConfig().operator bool()));
        descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::FolderId)
            .SetWritable(true)
            .SetWritePermission(EPermission::Administer)
            .SetReplicated(true)
            .SetRemovable(true)
            .SetPresent(account->GetFolderId().has_value()));
    }

    virtual bool GetBuiltinAttribute(TInternedAttributeKey key, NYson::IYsonConsumer* consumer) override
    {
        const auto& multicellManager = Bootstrap_->GetMulticellManager();
        const auto* account = GetThisImpl();

        switch (key) {
            case EInternedAttributeKey::ResourceUsage:
            case EInternedAttributeKey::CommittedResourceUsage:
            case EInternedAttributeKey::RecursiveResourceUsage:
            case EInternedAttributeKey::RecursiveCommittedResourceUsage: {
                auto committed =
                    key == EInternedAttributeKey::CommittedResourceUsage ||
                    key == EInternedAttributeKey::RecursiveCommittedResourceUsage;
                auto recursive =
                    key == EInternedAttributeKey::RecursiveResourceUsage ||
                    key == EInternedAttributeKey::RecursiveCommittedResourceUsage;

                if (!recursive && IsRootAccount()) {
                    break;
                }

                SerializeAccountClusterResourceUsage(
                    account,
                    committed,
                    recursive,
                    consumer,
                    Bootstrap_);

                return true;
            }

            case EInternedAttributeKey::MulticellStatistics: {
                if (IsRootAccount()) {
                    break;
                }

                BuildYsonFluently(consumer)
                    .DoMapFor(account->MulticellStatistics(), [&] (TFluentMap fluent, const std::pair<TCellTag, const TAccountStatistics&>& pair) {
                        fluent.Item(multicellManager->GetMasterCellName(pair.first));
                        Serialize(pair.second, fluent.GetConsumer(), Bootstrap_);
                    });
                return true;
            }

            case EInternedAttributeKey::AllowChildrenLimitOvercommit:
                if (IsRootAccount()) {
                    break;
                }
                BuildYsonFluently(consumer)
                    .Value(account->GetAllowChildrenLimitOvercommit());
                return true;

            case EInternedAttributeKey::ResourceLimits:
                if (IsRootAccount()) {
                    break;
                }
                SerializeClusterResourceLimits(
                    account->ClusterResourceLimits(),
                    consumer,
                    Bootstrap_,
                    /* serializeDiskSpace */ true);
                return true;

            case EInternedAttributeKey::ViolatedResourceLimits: {
                if (IsRootAccount()) {
                    break;
                }
                const auto& chunkManager = Bootstrap_->GetChunkManager();

                auto cellTags = multicellManager->GetSecondaryCellTags();
                cellTags.push_back(multicellManager->GetPrimaryCellTag());

                // TODO(shakurov): introduce TAccount::GetViolatedResourceLimits
                // and make use of it here.
                BuildYsonFluently(consumer)
                    .BeginMap()
                        .Item("disk_space").Value(account->IsDiskSpaceLimitViolated())
                        .Item("disk_space_per_medium").DoMapFor(chunkManager->Media(),
                            [&] (TFluentMap fluent, const std::pair<TMediumId, TMedium*>& pair) {
                                const auto* medium = pair.second;
                                fluent
                                    .Item(medium->GetName()).Value(account->IsDiskSpaceLimitViolated(medium->GetIndex()));
                            })
                        .Item("node_count").Value(account->IsNodeCountLimitViolated())
                        .Item("chunk_count").Value(account->IsChunkCountLimitViolated())
                        .Item("tablet_count").Value(account->IsTabletCountLimitViolated())
                        .Item("tablet_static_memory").Value(account->IsTabletStaticMemoryLimitViolated())
                        .Item("master_memory")
                            .BeginMap()
                                .Item("total").Value(account->IsMasterMemoryLimitViolated())
                                .Item("chunk_host").Value(account->IsChunkHostMasterMemoryLimitViolated(multicellManager))
                                .Item("per_cell").DoMapFor(cellTags, [&] (TFluentMap fluent, TCellTag cellTag) {
                                    fluent
                                        .Item(multicellManager->GetMasterCellName(cellTag)).Value(account->IsMasterMemoryLimitViolated(cellTag));
                                })
                            .EndMap()
                    .EndMap();
                return true;
            }

            case EInternedAttributeKey::RecursiveViolatedResourceLimits: {
                const auto& securityManager = Bootstrap_->GetSecurityManager();

                auto violatedLimits = securityManager->GetAccountRecursiveViolatedResourceLimits(account);
                SerializeClusterResourceLimits(violatedLimits, consumer, Bootstrap_, /* serializeDiskSpace */ false);
                return true;
            }

            case EInternedAttributeKey::TotalChildrenResourceLimits: {
                auto resourceLimits = account->ComputeTotalChildrenLimits();
                SerializeClusterResourceLimits(resourceLimits, consumer, Bootstrap_, /* serializeDiskSpace */ true);
                return true;
            }

            case EInternedAttributeKey::MergeJobRateLimit: {
                BuildYsonFluently(consumer)
                    .Value(account->GetMergeJobRateLimit());
                return true;
            }

            case EInternedAttributeKey::Abc: {
                if (account->GetAbcConfig()) {
                    BuildYsonFluently(consumer)
                        .Value(*account->GetAbcConfig());
                    return true;
                } else {
                    return false;
                }
            }

            case EInternedAttributeKey::FolderId: {
                if (account->GetFolderId()) {
                    BuildYsonFluently(consumer)
                        .Value(account->GetFolderId().value());
                    return true;
                } else {
                    return false;
                }
            }

            default:
                break;
        }

        return TBase::GetBuiltinAttribute(key, consumer);
    }

    virtual bool SetBuiltinAttribute(TInternedAttributeKey key, const NYson::TYsonString& value) override
    {
        auto* account = GetThisImpl();
        const auto& securityManager = Bootstrap_->GetSecurityManager();

        switch (key) {
            case EInternedAttributeKey::AllowChildrenLimitOvercommit: {
                if (IsRootAccount()) {
                    break;
                }

                auto overcommitAllowed = ConvertTo<bool>(value);
                securityManager->SetAccountAllowChildrenLimitOvercommit(account, overcommitAllowed);
                return true;
            }

            case EInternedAttributeKey::MergeJobRateLimit: {
                auto* user = securityManager->GetAuthenticatedUser();
                if (!securityManager->IsSuperuser(user)) {
                    THROW_ERROR_EXCEPTION(
                        NSecurityClient::EErrorCode::AuthorizationError,
                        "Access denied: only superusers can change merge job rate limit");
                }

                auto mergeJobRateLimit = ConvertTo<int>(value);
                account->SetMergeJobRateLimit(mergeJobRateLimit);
                return true;
            }

            case EInternedAttributeKey::ResourceLimits: {
                if (IsRootAccount()) {
                    break;
                }

                TClusterResourceLimits limits;
                DeserializeClusterResourceLimits(limits, ConvertToNode(value), Bootstrap_);
                securityManager->TrySetResourceLimits(account, limits);
                return true;
            }

            case EInternedAttributeKey::Abc: {
                account->SetAbcConfig(ConvertTo<NObjectClient::TAbcConfigPtr>(value));
                return true;
            }

            case EInternedAttributeKey::FolderId: {
                TString newFolderId = ConvertTo<TString>(value);
                ValidateFolderId(newFolderId);
                account->SetFolderId(std::move(newFolderId));
                return true;
            }

            default:
                break;
        }

        return TBase::SetBuiltinAttribute(key, value);
    }

    virtual bool RemoveBuiltinAttribute(TInternedAttributeKey key) override
    {
        auto* account = GetThisImpl();

        switch (key) {
            case EInternedAttributeKey::Abc: {
                account->SetAbcConfig(nullptr);
                return true;
            }

            case EInternedAttributeKey::FolderId: {
                account->SetFolderId(std::nullopt);
                return true;
            }

            default:
                break;
        }

        return TBase::RemoveBuiltinAttribute(key);
    }

    virtual bool DoInvoke(const NRpc::IServiceContextPtr& context) override
    {
        DISPATCH_YPATH_SERVICE_METHOD(TransferAccountResources);
        return TBase::DoInvoke(context);
    }

    DECLARE_YPATH_SERVICE_METHOD(NSecurityClient::NProto, TransferAccountResources)
    {
        Y_UNUSED(response);

        DeclareMutating();

        const auto& securityManager = Bootstrap_->GetSecurityManager();

        auto* impl = GetThisImpl();
        auto* srcAccount = securityManager->GetAccountByNameOrThrow(request->src_account(), true /*activeLifeStageOnly*/);

        TClusterResourceLimits resourceDelta;
        DeserializeClusterResourceLimits(resourceDelta, ConvertToNode(TYsonString(request->resource_delta())), Bootstrap_);

        context->SetRequestInfo("SrcAccount: %v, DstAccount: %v",
            srcAccount->GetName(),
            impl->GetName());

        securityManager->TransferAccountResources(srcAccount, impl, resourceDelta);

        context->Reply();

        if (IsPrimaryMaster()) {
            PostToSecondaryMasters(context);
        }
    }
};

TIntrusivePtr<TNonversionedMapObjectProxyBase<TAccount>> CreateAccountProxy(
    NCellMaster::TBootstrap* bootstrap,
    TObjectTypeMetadata* metadata,
    TAccount* account)
{
    return New<TAccountProxy>(bootstrap, metadata, account);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NSecurityServer

