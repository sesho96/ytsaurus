#include "private.h"
#include "public.h"
#include "lz.h"

#include <yt/contrib/quicklz/quicklz.h>

#define LZ4_DISABLE_DEPRECATE_WARNINGS

#include <contrib/libs/lz4/lz4.h>
#include <contrib/libs/lz4/lz4hc.h>

namespace NYT::NCompression {
namespace {

////////////////////////////////////////////////////////////////////////////////

/*
 * V0 wire format has no header at all.
 * Wire format goes simply as a sequence of blocks, each block is annotated
 * with a header of type TBlockHeader.
 *
 * V1 wire format has a preceding header which stores total uncompressed size
 * in 31-bit integer (sic!). Header structure is:
 *
 *   { i32 Signature; i32 Size; }
 *
 * V2 wire format has a preceding header which stores total uncompressed size
 * in 64-bit integer. Header structure is:
 *
 *   { ui32 Signature; ui32 Padding; ui64 Size; }
 *
 */

struct THeader
{
    static constexpr ui32 SignatureV1 = (1 << 30) + 1;
    static constexpr ui32 SignatureV2 = (1 << 30) + 2;

    ui32 Signature = static_cast<ui32>(-1);
    ui32 Size = 0;
};

struct TBlockHeader
{
    ui32 CompressedSize = 0;
    ui32 UncompressedSize = 0;
};

static_assert(
    sizeof(THeader) == sizeof(TBlockHeader),
    "Header and block header must be same size for backward compatibility.");

// TODO(sandello): Deprecate global MaxBlockSize.
static constexpr size_t MaxLzBlockSize = MaxBlockSize;

////////////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace NYT::NCompression

Y_DECLARE_PODTYPE(NYT::NCompression::THeader);
Y_DECLARE_PODTYPE(NYT::NCompression::TBlockHeader);

namespace NYT::NCompression {

////////////////////////////////////////////////////////////////////////////////

struct TLz4CompressedTag
{ };

struct TQuickLzCompressedTag
{ };

static inline bool ExtendedHeader(size_t totalUncompressedSize)
{
    return totalUncompressedSize > std::numeric_limits<i32>::max();
}

template <class TEstimateCompressedSizeFn>
static size_t GenericEstimateTotalCompressedSize(
    size_t totalUncompressedSize,
    TEstimateCompressedSizeFn&& estimateCompressedSizeFn)
{
    size_t result = sizeof(THeader);
    if (ExtendedHeader(totalUncompressedSize)) {
        result += sizeof(ui64);
    }
    // Estimate number of blocks, assuming that source feeds data in large chunks.
    size_t quotient = totalUncompressedSize / MaxLzBlockSize;
    if (quotient > 0) {
        result += quotient * (sizeof(TBlockHeader) + estimateCompressedSizeFn(MaxLzBlockSize));
    }
    size_t remainder = totalUncompressedSize - quotient * MaxLzBlockSize;
    if (remainder > 0) {
        result += sizeof(TBlockHeader) + estimateCompressedSizeFn(remainder);
    }
    return result;
}

template <class TEstimateCompressedSizeFn, class TCompressFn>
static void GenericBlockCompress(
    StreamSource* source,
    TBlob* sink,
    TEstimateCompressedSizeFn&& estimateCompressedSizeFn,
    TCompressFn&& compressFn)
{
    size_t totalUncompressedSize = source->Available();
    size_t totalCompressedSizeBound = GenericEstimateTotalCompressedSize(totalUncompressedSize, estimateCompressedSizeFn);

    sink->Reserve(totalCompressedSizeBound);
    YT_ASSERT(sink->IsEmpty());

    size_t currentPosition = 0;

    // Write out header.
    if (ExtendedHeader(totalUncompressedSize)) {
        THeader header;
        header.Signature = THeader::SignatureV2;
        header.Size = 0;
        TMemoryOutput memoryOutput(sink->Begin() + currentPosition, sizeof(THeader) + sizeof(ui64));
        WritePod(memoryOutput, header);
        WritePod(memoryOutput, static_cast<ui64>(totalUncompressedSize)); // Force serialization type.
        currentPosition += sizeof(THeader) + sizeof(ui64);
    } else {
        THeader header;
        header.Signature = THeader::SignatureV1;
        header.Size = static_cast<ui32>(totalUncompressedSize);
        TMemoryOutput memoryOutput(sink->Begin() + currentPosition, sizeof(THeader));
        WritePod(memoryOutput, header);
        currentPosition += sizeof(THeader);
    }

    // Write out body.
    while (totalUncompressedSize > 0) {
        YT_VERIFY(source->Available() == totalUncompressedSize);

        size_t inputSize = 0, inputOffset = 0;
        const char* input = source->Peek(&inputSize);

        inputSize = std::min(inputSize, totalUncompressedSize);

        while (inputSize > 0) {
            ui32 uncompressedSize = static_cast<ui32>(std::min(MaxLzBlockSize, inputSize));
            ui32 compressedSizeBound = estimateCompressedSizeFn(uncompressedSize);

            // XXX(sandello): We can underestimate output buffer size if source feeds data in tiny chunks.
            sink->Reserve(currentPosition + sizeof(TBlockHeader) + compressedSizeBound);

            auto compressedSize = compressFn(
                input + inputOffset,
                sink->Begin() + currentPosition + sizeof(TBlockHeader),
                uncompressedSize);
            // Non-positive return values indicate an error during compression.
            YT_VERIFY(compressedSize > 0);
            // COMPAT(sandello): Since block header may be interpreted as global header we have to make sure
            // that we never produce forward-incompatible signature (which aliases to compressed size).
            // Currently we treat all values <= 2^30 as proper sizes and all other values as signatures.
            YT_VERIFY(compressedSize <= MaxLzBlockSize);

            TBlockHeader header;
            header.CompressedSize = static_cast<ui32>(compressedSize);
            header.UncompressedSize = static_cast<ui32>(uncompressedSize);

            TMemoryOutput memoryOutput(sink->Begin() + currentPosition, sizeof(TBlockHeader));
            WritePod(memoryOutput, header);

            currentPosition += sizeof(TBlockHeader);
            currentPosition += header.CompressedSize;
            sink->Resize(currentPosition, false);

            inputSize -= uncompressedSize;
            inputOffset += uncompressedSize;
        }

        source->Skip(inputOffset);
        totalUncompressedSize -= inputOffset;
    }

    YT_VERIFY(source->Available() == 0);
}

template <class TTag, class TDecompressFn>
static void GenericBlockDecompress(StreamSource* source, TBlob* sink, TDecompressFn&& decompressFn)
{
    if (source->Available() == 0) {
        return;
    }

    ui64 totalUncompressedSize = 0;
    bool oldStyle = false;
    bool hasBlockHeader = false;

    TBlockHeader blockHeader;

    {
        THeader header;
        ReadPod(source, header);
        switch (header.Signature) {
            case THeader::SignatureV1:
                totalUncompressedSize = header.Size;
                break;

            case THeader::SignatureV2:
                ReadPod(source, totalUncompressedSize);
                break;

            default:
                // COMPAT(ignat): Needed to read old-style blocks.
                blockHeader.CompressedSize = header.Signature;
                blockHeader.UncompressedSize = header.Size;
                oldStyle = true;
                hasBlockHeader = true;
                totalUncompressedSize = 0;
                break;
        }
    }

    sink->Reserve(totalUncompressedSize);
    YT_ASSERT(sink->IsEmpty());

    auto inputBuffer = TBlob(TTag(), 0, false);

    while (source->Available() > 0) {
        if (Y_UNLIKELY(hasBlockHeader)) {
            hasBlockHeader = false;
        } else {
            ReadPod(source, blockHeader);
        }

        size_t oldSize = sink->Size();
        size_t newSize = oldSize + blockHeader.UncompressedSize;
        sink->Resize(newSize, false);

        size_t inputSize;
        const char* input = source->Peek(&inputSize);

        inputSize = std::min(inputSize, source->Available());

        // Fast-path; omit extra copy and feed data directly from source buffer.
        const bool hasCompleteBlock = inputSize >= blockHeader.CompressedSize;

        if (!hasCompleteBlock) {
            // Slow-path; coalesce input into a single slice.
            inputBuffer.Resize(blockHeader.CompressedSize, false);
            Read(source, inputBuffer.Begin(), inputBuffer.Size());
            input = inputBuffer.Begin();
        }

        decompressFn(input, blockHeader.CompressedSize, sink->Begin() + oldSize, blockHeader.UncompressedSize);

        if (hasCompleteBlock) {
            source->Skip(blockHeader.CompressedSize);
        }
    }

    if (!oldStyle) {
        YT_VERIFY(sink->Size() == totalUncompressedSize);
    }
}

void Lz4Compress(bool highCompression, StreamSource* source, TBlob* sink)
{
    const auto compressFn = highCompression ? LZ4_compressHC : LZ4_compress;

    GenericBlockCompress(
        source,
        sink,
        [] (size_t size) -> size_t {
            return static_cast<size_t>(LZ4_compressBound(static_cast<int>(size)));
        },
        compressFn);
}

void Lz4Decompress(StreamSource* source, TBlob* sink)
{
    GenericBlockDecompress<TLz4CompressedTag>(
        source,
        sink,
        [] (const char* input, size_t inputSize, char* output, size_t outputSize) {
            int rv = LZ4_decompress_fast(input, output, static_cast<int>(outputSize));
            YT_VERIFY(rv > 0 && rv == inputSize);
        }
    );
}

////////////////////////////////////////////////////////////////////////////////

void QuickLzCompress(StreamSource* source, TBlob* sink) {
    GenericBlockCompress(
        source,
        sink,
        [] (size_t size) -> size_t {
            return size + 400; // See QuickLZ implementation.
        },
        [] (const char *input, char *output, size_t inputSize) {
            qlz_state_compress state;
            return qlz_compress(input, output, inputSize, &state);
        });
}

void QuickLzDecompress(StreamSource* source, TBlob* sink)
{
    GenericBlockDecompress<TQuickLzCompressedTag>(
        source,
        sink,
        [] (const char* input, size_t inputSize, char* output, size_t outputSize) {
            qlz_state_decompress state;
            auto rv = qlz_decompress(input, output, &state);
            YT_VERIFY(rv > 0 && rv == outputSize);
        }
    );
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NCompression

