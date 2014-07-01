#pragma once

#include "public.h"

#include "chunk_meta_extensions.h"

#include <ytlib/chunk_client/chunk_meta_extensions.h>
#include <ytlib/chunk_client/chunk_reader_base.h>
#include <ytlib/chunk_client/public.h>
#include <ytlib/chunk_client/read_limit.h>
#include <ytlib/chunk_client/sequential_reader.h>

namespace NYT {
namespace NVersionedTableClient {

////////////////////////////////////////////////////////////////////////////////

class TChunkReaderBase
    : public virtual NChunkClient::IChunkReaderBase
{
public:
    TChunkReaderBase(
        TChunkReaderConfigPtr config,
        const NChunkClient::TReadLimit& lowerLimit,
        const NChunkClient::TReadLimit& upperLimit,
        NChunkClient::IReaderPtr underlyingReader,
        const NChunkClient::NProto::TMiscExt& misc);

    virtual TAsyncError Open() override;

    virtual TAsyncError GetReadyEvent() override;

    virtual NChunkClient::NProto::TDataStatistics GetDataStatistics() const;

    virtual TFuture<void> GetFetchingCompletedEvent();

protected:
    mutable NLog::TTaggedLogger Logger;

    TChunkReaderConfigPtr Config_;

    NChunkClient::TReadLimit LowerLimit_;
    NChunkClient::TReadLimit UpperLimit_;

    NChunkClient::IReaderPtr UnderlyingReader_;
    NChunkClient::TSequentialReaderPtr SequentialReader_;

    NChunkClient::NProto::TMiscExt Misc_;
    TAsyncError ReadyEvent_;

    bool BlockEnded_;

    TChunkedMemoryPool MemoryPool_;


    int GetBeginBlockIndex(const NProto::TBlockMetaExt& blockMeta) const;
    int GetBeginBlockIndex(
        const NProto::TBlockIndexExt& blockIndex,
        const NProto::TBoundaryKeysExt& boundaryKeys) const;

    int GetEndBlockIndex(const NProto::TBlockMetaExt& blockMeta) const;
    int GetEndBlockIndex(const NProto::TBlockIndexExt& blockIndex) const;

    TError DoOpen();

    TError DoSwitchBlock();

    bool OnBlockEnded();

    virtual std::vector<NChunkClient::TSequentialReader::TBlockInfo> GetBlockSequence() = 0;

    virtual void InitFirstBlock() = 0;
    virtual void InitNextBlock() = 0;

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NVersionedTableClient
} // namespace NYT
