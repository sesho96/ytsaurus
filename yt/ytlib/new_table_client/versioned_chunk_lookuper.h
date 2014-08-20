#pragma once

#include "public.h"

namespace NYT {
namespace NVersionedTableClient {

////////////////////////////////////////////////////////////////////////////////

IVersionedLookuperPtr CreateVersionedChunkLookuper(
    TChunkReaderConfigPtr config,
    NChunkClient::IReaderPtr chunkReader,
    NChunkClient::IBlockCachePtr uncompressedBlockCache,
    TCachedVersionedChunkMetaPtr chunkMeta,
    const TColumnFilter& columnFilter,
    TTimestamp timestamp = LastCommittedTimestamp);

////////////////////////////////////////////////////////////////////////////////

} // namespace NVersionedTableClient
} // namespace NYT
