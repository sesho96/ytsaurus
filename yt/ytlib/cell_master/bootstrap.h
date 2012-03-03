#pragma once

#include "public.h"

#include <ytlib/misc/property.h>
#include <ytlib/misc/enum.h>
#include <ytlib/actions/action_queue.h>
#include <ytlib/transaction_server/public.h>
#include <ytlib/cypress/public.h>
// TODO(babenko): replace with public.h
#include <ytlib/meta_state/meta_state_manager.h>

namespace NYT {
namespace NCellMaster {

////////////////////////////////////////////////////////////////////////////////

const int StateThreadPriorityCount = 1;

DECLARE_ENUM(EStateThreadPriority,
    ((Default)(0))
);

////////////////////////////////////////////////////////////////////////////////

class TBootstrap
{
public:
    TBootstrap(
        const Stroka& configFileName,
        TCellMasterConfig* config);

    ~TBootstrap();

    TCellMasterConfig* GetConfig() const;

    NTransactionServer::TTransactionManager* GetTransactionManager() const;
    NCypress::TCypressManager* GetCypressManager() const;
    NCypress::TWorldInitializer* GetWorldInitializer() const;
    NMetaState::IMetaStateManager* GetMetaStateManager() const;

    IInvoker* GetControlInvoker();
    IInvoker* GetStateInvoker(EStateThreadPriority priority = EStateThreadPriority::Default);

    void Run();

private:
    Stroka ConfigFileName;
    TCellMasterConfigPtr Config;

    NTransactionServer::TTransactionManagerPtr TransactionManager;
    NCypress::TCypressManagerPtr CypressManager;
    NCypress::TWorldInitializerPtr WorldInitializer;
    NMetaState::IMetaStateManager::TPtr MetaStateManager;

    TActionQueue::TPtr ControlQueue;
    TPrioritizedActionQueue::TPtr StateQueue;

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NCellMaster
} // namespace NYT
