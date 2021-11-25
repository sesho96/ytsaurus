#pragma once

#include <yt/yt/core/misc/public.h>

#include <yt/yt/client/hydra/public.h>

#include <yt/yt/client/object_client/public.h>

namespace NYT::NChaosClient {

////////////////////////////////////////////////////////////////////////////////

using TReplicationCardId = NObjectClient::TObjectId;
using TReplicaId = NObjectClient::TObjectId;
using TReplicationEra = ui64;

constexpr TReplicationEra InvalidReplicationEra = 0;
constexpr TReplicationEra InitialReplicationEra = 1;

DECLARE_REFCOUNTED_STRUCT(TReplicationCard)

DECLARE_REFCOUNTED_STRUCT(IReplicationCardCache)
DECLARE_REFCOUNTED_CLASS(TReplicationCardCacheConfig)

struct TReplicationProgress;
struct TReplicaHistoryItem;
struct TReplicaInfo;
struct TReplicationCardToken;

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTabletClient

