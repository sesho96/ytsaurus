#include "stdafx.h"
#include "chunk_list.h"

#include <ytlib/actions/invoker.h>
#include <ytlib/cell_master/load_context.h>

namespace NYT {
namespace NChunkServer {

using namespace NObjectServer;
using namespace NCellMaster;

////////////////////////////////////////////////////////////////////////////////

TChunkList::TChunkList(const TChunkListId& id)
    : TObjectWithIdBase(id)
    , Sorted_(false)
    , RebalancingEnabled_(true)
{ }

void TChunkList::Save(TOutputStream* output) const
{
    TObjectWithIdBase::Save(output);
    SaveObjectRefs(output, Children_);
    SaveObjectRefs(output, Parents_);
    ::Save(output, Statistics_);
    ::Save(output, Sorted_);
    ::Save(output, RebalancingEnabled_);
    ::Save(output, RowCountSums_);
}

void TChunkList::Load(const TLoadContext& context, TInputStream* input)
{
    UNUSED(context);
    TObjectWithIdBase::Load(input);
    LoadObjectRefs(input, Children_, context);
    LoadObjectRefs(input, Parents_, context);
    ::Load(input, Statistics_);
    ::Load(input, Sorted_);
    ::Load(input, RebalancingEnabled_);
    ::Load(input, RowCountSums_);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT
