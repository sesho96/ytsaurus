#pragma once

#include <yt/core/misc/public.h>

#include <yt/client/hydra/public.h>

#include <yt/client/object_client/public.h>

namespace NYT::NTabletClient {

////////////////////////////////////////////////////////////////////////////////

DEFINE_ENUM(ETabletState,
    // Individual states
    ((Mounting)        (0))
    ((Mounted)         (1))
    ((Unmounting)      (2))
    ((Unmounted)       (3))
    ((Freezing)        (4))
    ((Frozen)          (5))
    ((Unfreezing)      (6))
    ((FrozenMounting)  (7))

    // Special states
    ((None)          (100))
    ((Mixed)         (101))
    ((Transient)     (102))
);

constexpr ETabletState MinValidTabletState = ETabletState::Mounting;
constexpr ETabletState MaxValidTabletState = ETabletState::FrozenMounting;

// Keep in sync with NRpcProxy::NProto::ETableReplicaMode.
DEFINE_ENUM(ETableReplicaMode,
    ((Sync)     (0))
    ((Async)    (1))
);

DEFINE_ENUM(EErrorCode,
    ((TransactionLockConflict)  (1700))
    ((NoSuchTablet)             (1701))
    ((TabletNotMounted)         (1702))
    ((AllWritesDisabled)        (1703))
    ((InvalidMountRevision)     (1704))
    ((TableReplicaAlreadyExists)(1705))
    ((InvalidTabletState)       (1706))
    ((TableMountInfoNotReady)   (1707))
    ((TabletSnapshotExpired)    (1708))
);

DEFINE_ENUM(EInMemoryMode,
    ((None)        (0))
    ((Compressed)  (1))
    ((Uncompressed)(2))
);

using TTabletCellId = NHydra::TCellId;
extern const TTabletCellId NullTabletCellId;

using TTabletId = NObjectClient::TObjectId;
extern const TTabletId NullTabletId;

using TStoreId = NObjectClient::TObjectId;
extern const TStoreId NullStoreId;

using TPartitionId = NObjectClient::TObjectId;
extern const TPartitionId NullPartitionId;

using TTabletCellBundleId = NObjectClient::TObjectId;
extern const TTabletCellBundleId NullTabletCellBundleId;

using TTableReplicaId = NObjectClient::TObjectId;
using TTabletActionId = NObjectClient::TObjectId;

DEFINE_BIT_ENUM(EReplicationLogDataFlags,
    ((None)      (0x0000))
    ((Missing)   (0x0001))
    ((Aggregate) (0x0002))
);

struct TReplicationLogTable
{
    static const TString ChangeTypeColumnName;
    static const TString KeyColumnNamePrefix;
    static const TString ValueColumnNamePrefix;
    static const TString FlagsColumnNamePrefix;
};

DEFINE_BIT_ENUM(EUnversionedUpdateDataFlags,
    ((None)      (0x0000))
    ((Missing)   (0x0001))
    ((Aggregate) (0x0002))
);

constexpr EUnversionedUpdateDataFlags MinValidUnversionedUpdateDataFlags = EUnversionedUpdateDataFlags::None;
constexpr EUnversionedUpdateDataFlags MaxValidUnversionedUpdateDataFlags =
    EUnversionedUpdateDataFlags::Missing | EUnversionedUpdateDataFlags::Aggregate;

struct TUnversionedUpdateSchema
{
    static const TString ChangeTypeColumnName;
    static const TString ValueColumnNamePrefix;
    static const TString FlagsColumnNamePrefix;
};

DEFINE_ENUM(ETabletCellHealth,
    (Initializing)
    (Good)
    (Degraded)
    (Failed)
);

DEFINE_ENUM(ETableReplicaState,
    ((None)                     (0))
    ((Disabling)                (1))
    ((Disabled)                 (2))
    ((Enabling)                 (4))
    ((Enabled)                  (3))
);

DEFINE_ENUM(ETabletActionKind,
    ((Move)                     (0))
    ((Reshard)                  (1))
);

DEFINE_ENUM(ETabletActionState,
    ((Preparing)                (0))
    ((Freezing)                 (1))
    ((Frozen)                   (2))
    ((Unmounting)               (3))
    ((Unmounted)                (4))
    ((Orphaned)                (10))
    ((Mounting)                 (5))
    ((Mounted)                  (6))
    ((Completed)                (7))
    ((Failing)                  (8))
    ((Failed)                   (9))
);

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_CLASS(TTableMountCacheConfig)

DECLARE_REFCOUNTED_STRUCT(TTableMountInfo)
DECLARE_REFCOUNTED_STRUCT(TTabletInfo)
DECLARE_REFCOUNTED_STRUCT(TTableReplicaInfo)
DECLARE_REFCOUNTED_STRUCT(ITableMountCache)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTabletClient

