#include "stdafx.h"
#include "store_manager.h"
#include "tablet.h"
#include "dynamic_memory_store.h"
#include "config.h"
#include "tablet_slot.h"
#include "row_merger.h"
#include "private.h"

#include <core/misc/small_vector.h>

#include <core/concurrency/fiber.h>
#include <core/concurrency/parallel_collector.h>

#include <ytlib/object_client/public.h>

#include <ytlib/tablet_client/wire_protocol.h>

#include <ytlib/new_table_client/name_table.h>
#include <ytlib/new_table_client/versioned_row.h>
#include <ytlib/new_table_client/unversioned_row.h>
#include <ytlib/new_table_client/versioned_reader.h>
#include <ytlib/new_table_client/schemaful_reader.h>

#include <ytlib/tablet_client/config.h>

namespace NYT {
namespace NTabletNode {

using namespace NConcurrency;
using namespace NChunkClient;
using namespace NChunkClient::NProto;
using namespace NVersionedTableClient;
using namespace NTransactionClient;
using namespace NTabletClient;
using namespace NObjectClient;

using NVersionedTableClient::TKey;

////////////////////////////////////////////////////////////////////////////////

static const size_t MaxRowsPerRead = 1024;
static auto& Logger = TabletNodeLogger;

struct TLookupPoolTag { };

////////////////////////////////////////////////////////////////////////////////

TStoreManager::TStoreManager(
    TTabletManagerConfigPtr config,
    TTablet* Tablet_)
    : Config_(config)
    , Tablet_(Tablet_)
    , RotationScheduled_(false)
    , LookupPool_(GetRefCountedTrackerCookie<TLookupPoolTag>())
{
    YCHECK(Config_);
    YCHECK(Tablet_);

    VersionedPooledRows_.reserve(MaxRowsPerRead);
}

TStoreManager::~TStoreManager()
{ }

TTablet* TStoreManager::GetTablet() const
{
    return Tablet_;
}

bool TStoreManager::HasActiveLocks() const
{
    if (Tablet_->GetActiveStore()->GetLockCount() > 0) {
        return true;
    }
   
    if (!LockedStores_.empty()) {
        return true;
    }

    return false;
}

bool TStoreManager::HasUnflushedStores() const
{
    for (const auto& pair : Tablet_->Stores()) {
        const auto& store = pair.second;
        auto state = store->GetState();
        if (state != EStoreState::Persistent) {
            return true;
        }
    }
    return false;
}

void TStoreManager::LookupRows(
    TTimestamp timestamp,
    NTabletClient::TWireProtocolReader* reader,
    NTabletClient::TWireProtocolWriter* writer)
{
    auto columnFilter = reader->ReadColumnFilter();

    int keyColumnCount = Tablet_->GetKeyColumnCount();
    int schemaColumnCount = Tablet_->GetSchemaColumnCount();

    SmallVector<bool, TypicalColumnCount> columnFilterFlags(schemaColumnCount);
    if (columnFilter.All) {
        for (int id = 0; id < schemaColumnCount; ++id) {
            columnFilterFlags[id] = true;
        }
    } else {
        for (int index : columnFilter.Indexes) {
            if (index < 0 || index >= schemaColumnCount) {
                THROW_ERROR_EXCEPTION("Invalid index %d in column filter",
                    index);
            }
            columnFilterFlags[index] = true;
        }
    }

    PooledKeys_.clear();
    reader->ReadUnversionedRowset(&PooledKeys_);

    TUnversionedRowMerger rowMerger(
        &LookupPool_,
        schemaColumnCount,
        keyColumnCount,
        columnFilter);

    TKeyComparer keyComparer(keyColumnCount);

    UnversionedPooledRows_.clear();
    LookupPool_.Clear();

    for (auto pooledKey : PooledKeys_) {
        auto key = TOwningKey(pooledKey);
        auto keySuccessor = GetKeySuccessor(key.Get());

        // Construct readers.
        SmallVector<IVersionedReaderPtr, TypicalStoreCount> rowReaders;
        for (const auto& pair : Tablet_->Stores()) {
            const auto& store = pair.second;
            auto rowReader = store->CreateReader(
                key,
                keySuccessor,
                timestamp,
                columnFilter);
            if (rowReader) {
                rowReaders.push_back(std::move(rowReader));
            }
        }

        // Open readers.
        TIntrusivePtr<TParallelCollector<void>> openCollector;
        for (const auto& reader : rowReaders) {
            auto asyncResult = reader->Open();
            if (asyncResult.IsSet()) {
                THROW_ERROR_EXCEPTION_IF_FAILED(asyncResult.Get());
            } else {
                if (!openCollector) {
                    openCollector = New<TParallelCollector<void>>();
                }
                openCollector->Collect(asyncResult);
            }
        }

        if (openCollector) {
            auto result = WaitFor(openCollector->Complete());
            THROW_ERROR_EXCEPTION_IF_FAILED(result);
        }

        rowMerger.Start(key.Begin());

        // Merge values.
        for (const auto& reader : rowReaders) {
            VersionedPooledRows_.clear();
            // NB: Reading at most one row.
            reader->Read(&VersionedPooledRows_);
            if (VersionedPooledRows_.empty())
                continue;

            auto partialRow = VersionedPooledRows_[0];
            if (keyComparer(key, partialRow.BeginKeys()) != 0)
                continue;

            rowMerger.AddPartialRow(partialRow);
        }

        auto mergedRow = rowMerger.BuildMergedRow();
        UnversionedPooledRows_.push_back(mergedRow);
    }
    
    writer->WriteUnversionedRowset(UnversionedPooledRows_);
}

void TStoreManager::WriteRow(
    TTransaction* transaction,
    TUnversionedRow row,
    bool prewrite,
    std::vector<TDynamicRow>* lockedRows)
{
    auto rowRef = FindRowAndCheckLocks(
        transaction,
        row,
        ERowLockMode::Write);

    auto* store = rowRef.Store ? rowRef.Store : Tablet_->GetActiveStore().Get();

    auto updatedRow = store->WriteRow(
        transaction,
        row,
        prewrite);

    if (lockedRows && updatedRow) {
        lockedRows->push_back(updatedRow);
    }
}

void TStoreManager::DeleteRow(
    TTransaction* transaction,
    NVersionedTableClient::TKey key,
    bool prewrite,
    std::vector<TDynamicRow>* lockedRows)
{
    auto rowRef = FindRowAndCheckLocks(
        transaction,
        key,
        ERowLockMode::Delete);

    auto* store = rowRef.Store ? rowRef.Store : Tablet_->GetActiveStore().Get();

    auto updatedRow = store->DeleteRow(
        transaction,
        key,
        prewrite);

    if (lockedRows && updatedRow) {
        lockedRows->push_back(updatedRow);
    }
}

void TStoreManager::ConfirmRow(const TDynamicRowRef& rowRef)
{
    rowRef.Store->ConfirmRow(rowRef.Row);
}

void TStoreManager::PrepareRow(const TDynamicRowRef& rowRef)
{
    rowRef.Store->PrepareRow(rowRef.Row);
}

void TStoreManager::CommitRow(const TDynamicRowRef& rowRef)
{
    auto row = MigrateRowIfNeeded(rowRef);
    Tablet_->GetActiveStore()->CommitRow(row);
}

void TStoreManager::AbortRow(const TDynamicRowRef& rowRef)
{
    rowRef.Store->AbortRow(rowRef.Row);
    CheckForUnlockedStore(rowRef.Store);
}

TDynamicRow TStoreManager::MigrateRowIfNeeded(const TDynamicRowRef& rowRef)
{
    if (rowRef.Store->GetState() == EStoreState::ActiveDynamic) {
        return rowRef.Row;
    }

    auto* migrateFrom = rowRef.Store;
    const auto& migrateTo = Tablet_->GetActiveStore();
    auto migratedRow = migrateFrom->MigrateRow(rowRef.Row, migrateTo);

    CheckForUnlockedStore(migrateFrom);

    return migratedRow;
}

TDynamicRowRef TStoreManager::FindRowAndCheckLocks(
    TTransaction* transaction,
    TUnversionedRow key,
    ERowLockMode mode)
{
    for (const auto& store : LockedStores_) {
        auto row  = store->FindRowAndCheckLocks(
            key,
            transaction,
            ERowLockMode::Write);
        if (row) {
            return TDynamicRowRef(store.Get(), row);
        }
    }

    // TODO(babenko): check passive stores for write timestamps

    return TDynamicRowRef();
}

void TStoreManager::CheckForUnlockedStore(const TDynamicMemoryStorePtr& store)
{
    if (store == Tablet_->GetActiveStore() || store->GetLockCount() > 0)
        return;

    LOG_INFO("Store unlocked and will be dropped (TabletId: %s, StoreId: %s)",
        ~ToString(Tablet_->GetId()),
        ~ToString(store->GetId()));
    YCHECK(LockedStores_.erase(store) == 1);
}

bool TStoreManager::IsRotationNeeded() const
{
    if (RotationScheduled_) {
        return false;
    }

    const auto& store = Tablet_->GetActiveStore();
    const auto& config = Tablet_->GetConfig();
    return
        store->GetKeyCount() >= config->KeyCountFlushThreshold ||
        store->GetValueCount() >= config->ValueCountFlushThreshold ||
        store->GetAlignedPoolSize() >= config->AlignedPoolSizeFlushThreshold ||
        store->GetUnalignedPoolSize() >= config->UnalignedPoolSizeFlushThreshold;
}

void TStoreManager::SetRotationScheduled()
{
    if (RotationScheduled_) 
        return;
    
    RotationScheduled_ = true;

    LOG_INFO("Tablet store rotation scheduled (TabletId: %s)",
        ~ToString(Tablet_->GetId()));
}

void TStoreManager::ResetRotationScheduled()
{
    if (!RotationScheduled_)
        return;

    RotationScheduled_ = false;

    LOG_INFO("Tablet store rotation canceled (TabletId: %s)",
        ~ToString(Tablet_->GetId()));
}

void TStoreManager::Rotate(bool createNew)
{
    RotationScheduled_ = false;

    auto activeStore = Tablet_->GetActiveStore();
    YCHECK(activeStore);
    activeStore->SetState(EStoreState::PassiveDynamic);

    if (activeStore->GetLockCount() > 0) {
        LOG_INFO("Active store is locked and will be kept (TabletId: %s, StoreId: %s, LockCount: %d)",
            ~ToString(Tablet_->GetId()),
            ~ToString(activeStore->GetId()),
            activeStore->GetLockCount());
        YCHECK(LockedStores_.insert(activeStore).second);
    }

    if (createNew) {
        CreateActiveStore();
    } else {
        Tablet_->SetActiveStore(nullptr);
    }

    LOG_INFO("Tablet stores rotated (TabletId: %s)",
        ~ToString(Tablet_->GetId()));
}

void TStoreManager::CreateActiveStore()
{
    auto* slot = Tablet_->GetSlot();
    // NB: For tests mostly.
    auto id = slot ? slot->GenerateId(EObjectType::DynamicMemoryTabletStore) : TStoreId::Create();
 
    auto store = New<TDynamicMemoryStore>(
        Config_,
        id,
        Tablet_);

    Tablet_->AddStore(store);
    Tablet_->SetActiveStore(store);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletNode
} // namespace NYT
