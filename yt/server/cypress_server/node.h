#pragma once

#include "public.h"
#include "lock.h"

#include <ytlib/cypress_client/public.h>

#include <server/cell_master/public.h>

#include <server/transaction_server/public.h>

#include <server/object_server/public.h>

#include <server/security_server/cluster_resources.h>

namespace NYT {
namespace NCypressServer {

////////////////////////////////////////////////////////////////////////////////

//! Provides a common interface for all persistent nodes.
struct ICypressNode
    : private TNonCopyable
{
    virtual ~ICypressNode()
    { }

    //! Returns node type.
    virtual NObjectClient::EObjectType GetObjectType() const = 0;

    //! Saves the node into the snapshot stream.
    virtual void Save(const NCellMaster::TSaveContext& context) const = 0;
    
    //! Loads the node from the snapshot stream.
    virtual void Load(const NCellMaster::TLoadContext& context) = 0;

    //! Returns the composite (versioned) id of the node.
    virtual const TVersionedNodeId& GetId() const = 0;

    virtual ELockMode GetLockMode() const = 0;
    virtual void SetLockMode(ELockMode mode) = 0;

    //! Returns the trunk node, i.e. for a node with id |(objectId, transactionId)| returns
    //! the node with id |(objectId, NullTransactionId)|.
    virtual ICypressNode* GetTrunkNode() const = 0;
    //! Used internally to set the trunk node during branching.
    virtual void SetTrunkNode(ICypressNode* trunkNode) = 0;

    //! Returns the transaction for which the node is branched (NULL if in trunk).
    virtual NTransactionServer::TTransaction* GetTransaction() const = 0;
    //! Used internally to set the transaction during branching.
    virtual void SetTransaction(NTransactionServer::TTransaction* transaction) = 0;

    //! Gets the parent node id.
    virtual TNodeId GetParentId() const = 0;
    //! Sets the parent node id.
    virtual void SetParentId(TNodeId value) = 0;

    typedef yhash_map<NTransactionServer::TTransaction*, TLock> TLockMap;

    // Transaction-to-lock map.
    virtual const TLockMap& Locks() const = 0;
    virtual TLockMap& Locks() = 0;
    
    virtual TInstant GetCreationTime() const = 0;
    virtual void SetCreationTime(TInstant value) = 0;

    virtual TInstant GetModificationTime() const = 0;
    virtual void SetModificationTime(TInstant value) = 0;

    //! Increments the reference counter, returns the incremented value.
    virtual int RefObject() = 0;
    //! Decrements the reference counter, returns the decremented value.
    virtual int UnrefObject() = 0;
    //! Returns the current reference counter value.
    virtual int GetObjectRefCounter() const = 0;
    //! Returns True iff the reference counter is positive.
    virtual bool IsAlive() const = 0;

    //! Implemented by nodes that own chunk trees (i.e. files and tables).
    virtual int GetOwningReplicationFactor() const = 0;

    virtual NSecurityServer::TAccount* GetAccount() const = 0;
    virtual void SetAccount(NSecurityServer::TAccount* account) = 0;

    //! Returns resources used by the object.
    /*!
     *  For branched nodes this is typically a delta from the baseline.
     *  Values returned by #ICypressNode::GetResourceUsage are used for accounting.
     *
     *  \see #ICypressNodeProxy::GetResourceUsage
     */
    virtual NSecurityServer::TClusterResources GetResourceUsage() const = 0;

    // Resource usage last observed by Security Manager.
    virtual const NSecurityServer::TClusterResources& CachedResourceUsage() const = 0;
    virtual NSecurityServer::TClusterResources& CachedResourceUsage() = 0;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NCypressServer
} // namespace NYT

////////////////////////////////////////////////////////////////////////////////

// TObjectIdTraits and GetObjectId specializations.

namespace NYT {
namespace NObjectServer {

template <>
struct TObjectIdTraits<NCypressServer::ICypressNode*, void>
{
    typedef TVersionedObjectId TId;
};

template <class T>
TVersionedObjectId GetObjectId(
    T object,
    typename NMpl::TEnableIf< NMpl::TIsConvertible<T, NCypressServer::ICypressNode*>, void* >::TType = NULL)
{
    return object ? object->GetId() : TVersionedObjectId(NullObjectId, NullTransactionId);
}

} // namespace NObjectServer
} // namespace NYT

////////////////////////////////////////////////////////////////////////////////
