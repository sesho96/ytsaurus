#pragma once

#include "public.h"

#include <yt/yt_proto/yt/client/table_chunk_format/proto/chunk_meta.pb.h>

#include <yt/yt/core/misc/ref.h>

namespace NYT::NTableClient {

////////////////////////////////////////////////////////////////////////////////

struct TBlock
{
    std::vector<TSharedRef> Data;
    NProto::TDataBlockMeta Meta;
    std::optional<int> GroupIndex;
};

////////////////////////////////////////////////////////////////////////////////

struct IBlockWriter
{
    virtual ~IBlockWriter() = default;

    virtual i64 GetBlockSize() const = 0;
    virtual i64 GetRowCount() const = 0;

    virtual TBlock FlushBlock() = 0;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTableClient
