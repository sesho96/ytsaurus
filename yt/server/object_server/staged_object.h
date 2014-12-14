#pragma once

#include "public.h"

#include <core/misc/property.h>

#include <server/transaction_server/public.h>

#include <server/security_server/public.h>

#include <server/object_server/object_detail.h>

#include <server/cell_master/public.h>

namespace NYT {
namespace NObjectServer {

////////////////////////////////////////////////////////////////////////////////

class TStagedObject
    : public NObjectServer::TNonversionedObjectBase
{
public:
    DEFINE_BYVAL_RW_PROPERTY(NTransactionServer::TTransaction*, StagingTransaction);
    DEFINE_BYVAL_RW_PROPERTY(NSecurityServer::TAccount*, StagingAccount);

public:
    explicit TStagedObject(const TObjectId& id);

    void Save(NCellMaster::TSaveContext& context) const;
    void Load(NCellMaster::TLoadContext& context);

    //! Returns True if the object is the staging area of some transaction.
    bool IsStaged() const;

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NObjectServer
} // namespace NYT
