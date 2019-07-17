#pragma once
#ifndef CHUNK_REPLICA_INL_H_
#error "Direct inclusion of this file is not allowed, include chunk_replica.h"
// For the sake of sane code completion.
#include "chunk_replica.h"
#endif

namespace NYT::NChunkClient {

////////////////////////////////////////////////////////////////////////////////

Y_FORCE_INLINE TChunkReplicaWithMedium::TChunkReplicaWithMedium()
    : TChunkReplicaWithMedium(NNodeTrackerClient::InvalidNodeId)
{ }

Y_FORCE_INLINE TChunkReplicaWithMedium::TChunkReplicaWithMedium(ui64 value)
    : Value(value)
{ }

Y_FORCE_INLINE TChunkReplicaWithMedium::TChunkReplicaWithMedium(int nodeId, int replicaIndex, int mediumIndex)
    : Value(static_cast<ui64>(nodeId) | (static_cast<ui64>(replicaIndex) << 24) | (static_cast<ui64>(mediumIndex) << 29))
{
    static_assert(
        ChunkReplicaIndexBound * MediumIndexBound <= 0x1000,
        "Replica and medium indexes must fit into 12 bits.");

    YT_ASSERT(nodeId >= 0 && nodeId <= static_cast<int>(NNodeTrackerClient::MaxNodeId));
    YT_ASSERT(replicaIndex >= 0 && replicaIndex < ChunkReplicaIndexBound);
    YT_ASSERT(mediumIndex >= 0 && mediumIndex < MediumIndexBound);
}

Y_FORCE_INLINE int TChunkReplicaWithMedium::GetNodeId() const
{
    return Value & 0x00ffffff;
}

Y_FORCE_INLINE int TChunkReplicaWithMedium::GetReplicaIndex() const
{
    return (Value & 0x1f000000) >> 24;
}

Y_FORCE_INLINE int TChunkReplicaWithMedium::GetMediumIndex() const
{
    return Value >> 29;
}

Y_FORCE_INLINE void ToProto(ui64* value, TChunkReplicaWithMedium replica)
{
    *value = replica.Value;
}

Y_FORCE_INLINE void FromProto(TChunkReplicaWithMedium* replica, ui64 value)
{
    replica->Value = value;
}

// COMPAT(aozeritsky)
Y_FORCE_INLINE void ToProto(ui32* value, TChunkReplicaWithMedium replica)
{
    *value = replica.Value;
}

Y_FORCE_INLINE void FromProto(TChunkReplicaWithMedium* replica, ui32 value)
{
    replica->Value = value;
}

Y_FORCE_INLINE TChunkReplica::TChunkReplica()
    : TChunkReplica(NNodeTrackerClient::InvalidNodeId)
{ }

Y_FORCE_INLINE TChunkReplica::TChunkReplica(ui32 value)
    : Value(value)
{ }

Y_FORCE_INLINE TChunkReplica::TChunkReplica(int nodeId, int replicaIndex)
    : Value(static_cast<ui64>(nodeId) | (static_cast<ui64>(replicaIndex) << 24))
{
    YT_ASSERT(nodeId >= 0 && nodeId <= static_cast<int>(NNodeTrackerClient::MaxNodeId));
    YT_ASSERT(replicaIndex >= 0 && replicaIndex < ChunkReplicaIndexBound);
}

Y_FORCE_INLINE TChunkReplica::TChunkReplica(const TChunkReplicaWithMedium& replica)
    : Value(static_cast<ui64>(replica.GetNodeId()) | (static_cast<ui64>(replica.GetReplicaIndex()) << 24))
{ }

Y_FORCE_INLINE int TChunkReplica::GetNodeId() const
{
    return Value & 0x00ffffff;
}

Y_FORCE_INLINE int TChunkReplica::GetReplicaIndex() const
{
    return (Value & 0x1f000000) >> 24;
}

Y_FORCE_INLINE void ToProto(ui32* value, TChunkReplica replica)
{
    *value = replica.Value;
}

Y_FORCE_INLINE void FromProto(TChunkReplica* replica, ui32 value)
{
    replica->Value = value;
}

////////////////////////////////////////////////////////////////////////////////

Y_FORCE_INLINE TChunkIdWithIndex::TChunkIdWithIndex()
    : ReplicaIndex(GenericChunkReplicaIndex)
{ }

Y_FORCE_INLINE TChunkIdWithIndex::TChunkIdWithIndex(TChunkId id, int replicaIndex)
    : Id(id)
    , ReplicaIndex(replicaIndex)
{ }

Y_FORCE_INLINE bool operator==(const TChunkIdWithIndex& lhs, const TChunkIdWithIndex& rhs)
{
    return lhs.Id == rhs.Id && lhs.ReplicaIndex == rhs.ReplicaIndex;
}

Y_FORCE_INLINE bool operator!=(const TChunkIdWithIndex& lhs, const TChunkIdWithIndex& rhs)
{
    return !(lhs == rhs);
}

////////////////////////////////////////////////////////////////////////////////

Y_FORCE_INLINE TChunkIdWithIndexes::TChunkIdWithIndexes()
    : TChunkIdWithIndex()
    , MediumIndex(DefaultStoreMediumIndex)
{ }

Y_FORCE_INLINE TChunkIdWithIndexes::TChunkIdWithIndexes(const TChunkIdWithIndex& chunkIdWithIndex, int mediumIndex)
    : TChunkIdWithIndex(chunkIdWithIndex)
    , MediumIndex(mediumIndex)
{ }

Y_FORCE_INLINE TChunkIdWithIndexes::TChunkIdWithIndexes(TChunkId id, int replicaIndex, int mediumIndex)
    : TChunkIdWithIndex(id, replicaIndex)
    , MediumIndex(mediumIndex)
{ }

Y_FORCE_INLINE bool operator==(const TChunkIdWithIndexes& lhs, const TChunkIdWithIndexes& rhs)
{
    return static_cast<const TChunkIdWithIndex&>(lhs) == static_cast<const TChunkIdWithIndex&>(rhs) &&
        lhs.MediumIndex == rhs.MediumIndex;
}

Y_FORCE_INLINE bool operator!=(const TChunkIdWithIndexes& lhs, const TChunkIdWithIndexes& rhs)
{
    return !(lhs == rhs);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NChunkClient
