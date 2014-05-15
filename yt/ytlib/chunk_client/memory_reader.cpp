#include "memory_reader.h"
#include "chunk_meta_extensions.h"

namespace NYT {
namespace NChunkClient {

///////////////////////////////////////////////////////////////////////////////

TMemoryReader::TMemoryReader(
    NProto::TChunkMeta chunkMeta,
    std::vector<TSharedRef> blocks)
    : ChunkMeta(std::move(chunkMeta))
    , Blocks(std::move(blocks))
{ }

auto TMemoryReader::ReadBlocks(const std::vector<int>& blockIndexes) -> TAsyncReadResult
{
    std::vector<TSharedRef> blocks;
    for (auto index: blockIndexes) {
        YCHECK(index < Blocks.size());
        blocks.push_back(Blocks[index]);
    }

    return MakeFuture(TReadResult(std::move(blocks)));
}

auto TMemoryReader::GetChunkMeta(
    const TNullable<int>& partitionTag,
    const std::vector<int>* extensionTags) -> TAsyncGetMetaResult
{
    YCHECK(!partitionTag);

    return MakeFuture(TGetMetaResult(
        extensionTags
        ? FilterChunkMetaByExtensionTags(ChunkMeta, *extensionTags)
        : ChunkMeta));
}

TChunkId TMemoryReader::GetChunkId() const
{
    // ToDo(psushin): make YUNIMPLEMENTED, after fixing sequential reader.
    return NullChunkId;
}

///////////////////////////////////////////////////////////////////////////////

} // namespace NChunkClient
} // namespace NYT
