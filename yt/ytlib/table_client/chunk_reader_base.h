#pragma once

#include "public.h"
#include "chunk_meta_extensions.h"

#include <yt/ytlib/chunk_client/chunk_meta_extensions.h>
#include <yt/ytlib/chunk_client/reader_base.h>
#include <yt/ytlib/chunk_client/public.h>
#include <yt/ytlib/chunk_client/read_limit.h>
#include <yt/ytlib/chunk_client/sequential_reader.h>

namespace NYT {
namespace NTableClient {

////////////////////////////////////////////////////////////////////////////////

class TChunkReaderBase
    : public virtual NChunkClient::IReaderBase
{
public:
    TChunkReaderBase(
        NChunkClient::TSequentialReaderConfigPtr config,
        NChunkClient::IChunkReaderPtr underlyingReader,
        NChunkClient::IBlockCachePtr blockCache);

    virtual TFuture<void> GetReadyEvent() override;

    virtual NChunkClient::NProto::TDataStatistics GetDataStatistics() const override;

    virtual bool IsFetchingCompleted() const override;

    virtual std::vector<NChunkClient::TChunkId> GetFailedChunkIds() const override;

protected:
    const NChunkClient::TSequentialReaderConfigPtr Config_;
    const NChunkClient::IBlockCachePtr BlockCache_;
    const NChunkClient::IChunkReaderPtr UnderlyingReader_;

    NChunkClient::TSequentialReaderPtr SequentialReader_;
    TFuture<void> ReadyEvent_ = VoidFuture;

    bool BlockEnded_ = false;
    bool InitFirstBlockNeeded_ = false;
    bool InitNextBlockNeeded_ = false;

    bool CheckRowLimit_ = false;
    bool CheckKeyLimit_ = false;

    TChunkedMemoryPool MemoryPool_;

    NLogging::TLogger Logger;


    bool BeginRead();
    bool OnBlockEnded();

    TFuture<void> DoOpen(
        std::vector<NChunkClient::TSequentialReader::TBlockInfo> blockSequence,
        const NChunkClient::NProto::TMiscExt& miscExt);

    static int GetBlockIndexByKey(
        const TKey& key,
        const std::vector<TOwningKey>& blockIndexKeys,
        int beginBlockIndex = 0);

    void CheckBlockUpperLimits(
        const NProto::TBlockMeta& blockMeta,
        const NChunkClient::TReadLimit& upperLimit,
        TNullable<int> keyColumnCount = Null);

    // These methods return min block index, satisfying the lower limit.
    int ApplyLowerRowLimit(const NProto::TBlockMetaExt& blockMeta, const NChunkClient::TReadLimit& lowerLimit) const;
    int ApplyLowerKeyLimit(const NProto::TBlockMetaExt& blockMeta, const NChunkClient::TReadLimit& lowerLimit) const;
    int ApplyLowerKeyLimit(const std::vector<TOwningKey>& blockIndexKeys, const NChunkClient::TReadLimit& lowerLimit) const;

    // These methods return max block index, satisfying the upper limit.
    int ApplyUpperRowLimit(const NProto::TBlockMetaExt& blockMeta, const NChunkClient::TReadLimit& upperLimit) const;
    int ApplyUpperKeyLimit(const NProto::TBlockMetaExt& blockMeta, const NChunkClient::TReadLimit& upperLimit) const;
    int ApplyUpperKeyLimit(const std::vector<TOwningKey>& blockIndexKeys, const NChunkClient::TReadLimit& upperLimit) const;

    virtual void InitFirstBlock() = 0;
    virtual void InitNextBlock() = 0;

private:
    std::vector<TUnversionedValue> WidenKey(const TOwningKey& key, int keyColumnCount);

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NTableClient
} // namespace NYT
