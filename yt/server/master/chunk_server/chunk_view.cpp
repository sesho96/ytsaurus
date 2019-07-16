#include "chunk_view.h"

#include "helpers.h"

#include <yt/server/master/cell_master/serialize.h>

namespace NYT::NChunkServer {

using namespace NObjectClient;
using namespace NChunkClient;

////////////////////////////////////////////////////////////////////////////////

TChunkView::TChunkView(const TChunkViewId& id)
    : TChunkTree(id)
{ }

void TChunkView::SetUnderlyingChunk(TChunk* underlyingChunk)
{
    YT_VERIFY(underlyingChunk);
    auto chunkType = EChunkType(underlyingChunk->ChunkMeta().type());
    YT_VERIFY(chunkType == EChunkType::Table);

    UnderlyingChunk_ = underlyingChunk;
}

void TChunkView::SetReadRange(TReadRange readRange)
{
    YT_VERIFY(!readRange.LowerLimit().HasOffset());
    YT_VERIFY(!readRange.UpperLimit().HasOffset());
    YT_VERIFY(!readRange.LowerLimit().HasChunkIndex());
    YT_VERIFY(!readRange.UpperLimit().HasChunkIndex());
    YT_VERIFY(!readRange.LowerLimit().HasRowIndex());
    YT_VERIFY(!readRange.UpperLimit().HasRowIndex());

    ReadRange_ = std::move(readRange);

    if (readRange.UpperLimit().HasKey()) {
        const auto& key = readRange.UpperLimit().GetKey();
        YT_VERIFY(key != NTableClient::MaxKey());
    }
}

void TChunkView::Save(NCellMaster::TSaveContext& context) const
{
    TChunkTree::Save(context);

    using NYT::Save;

    Save(context, UnderlyingChunk_);
    Save(context, ReadRange_);
    Save(context, Parents_);
}

void TChunkView::Load(NCellMaster::TLoadContext& context)
{
    TChunkTree::Load(context);

    using NYT::Load;

    Load(context, UnderlyingChunk_);
    Load(context, ReadRange_);
    Load(context, Parents_);
}

TReadLimit TChunkView::GetAdjustedLowerReadLimit(TReadLimit readLimit) const
{
    if (ReadRange_.LowerLimit().HasKey()) {
        readLimit.MergeLowerKey(ReadRange_.LowerLimit().GetKey());
    }
    return readLimit;
}

TReadLimit TChunkView::GetAdjustedUpperReadLimit(TReadLimit readLimit) const
{
    if (ReadRange_.UpperLimit().HasKey()) {
        readLimit.MergeUpperKey(ReadRange_.UpperLimit().GetKey());
    }
    return readLimit;
}

TReadRange TChunkView::GetCompleteReadRange() const
{
    return {
        GetAdjustedLowerReadLimit(TReadLimit(GetMinKeyOrThrow(UnderlyingChunk_))),
        GetAdjustedUpperReadLimit(TReadLimit(GetUpperBoundKeyOrThrow(UnderlyingChunk_)))
    };
}

void TChunkView::AddParent(TChunkList* parent)
{
    Parents_.push_back(parent);
}

void TChunkView::RemoveParent(TChunkList* parent)
{
    auto it = std::find(Parents_.begin(), Parents_.end(), parent);
    YT_VERIFY(it != Parents_.end());
    Parents_.erase(it);
}

TChunkTreeStatistics TChunkView::GetStatistics() const
{
    return UnderlyingChunk_->GetStatistics();
}

int CompareButForReadRange(const TChunkView* lhs, const TChunkView* rhs)
{
    // TODO(ifsmirnov): when ChunkView gets new attributes (e.g. tx_id) one should
    // consider them here and merge only views with identical attributes.

    const auto& lhsChunkId = lhs->GetUnderlyingChunk()->GetId();
    const auto& rhsChunkId = rhs->GetUnderlyingChunk()->GetId();
    if (lhsChunkId == rhsChunkId) {
        return 0;
    } else {
        return lhsChunkId < rhsChunkId ? -1 : 1;
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NChunkServer
