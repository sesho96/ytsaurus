#pragma once

#include "public.h"
#include <ytlib/chunk_server/chunk_service.pb.h>

namespace NYT {
namespace NChunkServer {

////////////////////////////////////////////////////////////////////////////////

struct TTotalNodeStatistics
{
    i64 AvailbaleSpace;
    i64 UsedSpace;
    i32 ChunkCount;
    i32 SessionCount;
    i32 OnlineNodeCount;

    TTotalNodeStatistics()
        : AvailbaleSpace(0)
        , UsedSpace(0)
        , ChunkCount(0)
        , SessionCount(0)
        , OnlineNodeCount(0)
    { }
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT

////////////////////////////////////////////////////////////////////////////////

Stroka ToString(const NYT::NChunkServer::NProto::TNodeStatistics& statistics);

////////////////////////////////////////////////////////////////////////////////

