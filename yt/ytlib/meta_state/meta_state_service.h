#pragma once

#include <ytlib/actions/action.h>
#include <ytlib/rpc/service.h>
#include <ytlib/meta_state/meta_state_manager.h>

namespace NYT {
namespace NMetaState {

////////////////////////////////////////////////////////////////////////////////

class TMetaStateServiceBase
    : public NRpc::TServiceBase
{
protected:
    typedef TIntrusivePtr<TMetaStateServiceBase> TPtr;

    IMetaStateManager::TPtr MetaStateManager;

    TMetaStateServiceBase(
        IMetaStateManager* metaStateManager,
        const Stroka& serviceName,
        const Stroka& loggingCategory)
        : NRpc::TServiceBase(
            ~metaStateManager->GetStateInvoker(),
            serviceName,
            loggingCategory)
        , MetaStateManager(metaStateManager)
    {
        YASSERT(metaStateManager);
    }

    template <class TContext>
    IParamAction<TVoid>::TPtr CreateSuccessHandler(TContext* context)
    {
        TIntrusivePtr<TContext> context_ = context;
        return FromFunctor([=] (TVoid)
            {
                context_->Reply();
            });
    }

    template <class TContext>
    IAction::TPtr CreateErrorHandler(TContext* context)
    {
        TIntrusivePtr<TContext> context_ = context;
        return FromFunctor([=] ()
            {
                context_->Reply(
                    NRpc::EErrorCode::Unavailable,
                    "Error committing meta state changes");
            });
    }

    void ValidateLeader()
    {
        if (MetaStateManager->GetStateStatus() != EPeerStatus::Leading) {
            ythrow NRpc::TServiceException(NRpc::EErrorCode::Unavailable) <<
                "Not a leader";
        }
        if (!MetaStateManager->HasActiveQuorum()) {
            ythrow NRpc::TServiceException(NRpc::EErrorCode::Unavailable) <<
                "Leader currently has no active quorum";
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NMetaState
} // namespace NYT
