#pragma once

#include "public.h"

#include <core/misc/property.h>

#include <core/actions/cancelable_context.h>

#include <ytlib/new_table_client/schema.h>

#include <ytlib/tablet_client/public.h>

#include <ytlib/chunk_client/public.h>

namespace NYT {
namespace NTabletNode {

////////////////////////////////////////////////////////////////////////////////

class TTablet
    : public TNonCopyable
{
public:
    DEFINE_BYVAL_RO_PROPERTY(TTabletId, Id);
    DEFINE_BYVAL_RO_PROPERTY(TTabletSlot*, Slot);
    DEFINE_BYREF_RO_PROPERTY(NVersionedTableClient::TTableSchema, Schema);
    DEFINE_BYREF_RO_PROPERTY(NVersionedTableClient::TKeyColumns, KeyColumns);
    
    DEFINE_BYVAL_RW_PROPERTY(ETabletState, State);

    DEFINE_BYVAL_RW_PROPERTY(TCancelableContextPtr, CancelableContext);
    DEFINE_BYVAL_RW_PROPERTY(IInvokerPtr, EpochAutomatonInvoker);

public:
    explicit TTablet(const TTabletId& id);
    TTablet(
        const TTabletId& id,
        TTabletSlot* slot,
        const NVersionedTableClient::TTableSchema& schema,
        const NVersionedTableClient::TKeyColumns& keyColumns);

    ~TTablet();

    const NTabletClient::TTableMountConfigPtr& GetConfig() const;
    void SetConfig(NTabletClient::TTableMountConfigPtr config);

    const NVersionedTableClient::TNameTablePtr& GetNameTable() const;

    const TStoreManagerPtr& GetStoreManager() const;
    void SetStoreManager(TStoreManagerPtr manager);

    const yhash_map<TStoreId, IStorePtr>& Stores() const;

    void AddStore(IStorePtr store);
    void RemoveStore(const TStoreId& id);
    IStorePtr FindStore(const TStoreId& id);
    IStorePtr GetStore(const TStoreId& id);

    const TDynamicMemoryStorePtr& GetActiveStore() const;
    void SetActiveStore(TDynamicMemoryStorePtr store);

    void Save(TSaveContext& context) const;
    void Load(TLoadContext& context);

    int GetSchemaColumnCount() const;
    int GetKeyColumnCount() const;

private:
    NTabletClient::TTableMountConfigPtr Config_;
    NVersionedTableClient::TNameTablePtr NameTable_;
    TStoreManagerPtr StoreManager_;
    
    TDynamicMemoryStorePtr ActiveStore_;
    yhash_map<TStoreId, IStorePtr> Stores_;

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletNode
} // namespace NYT
