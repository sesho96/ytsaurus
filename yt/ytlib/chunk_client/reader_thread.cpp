﻿#include "stdafx.h"

#include "reader_thread.h"

namespace NYT {
namespace NChunkClient {

////////////////////////////////////////////////////////////////////////////////

TLazyPtr<TActionQueue> ReaderThread(TActionQueue::CreateFactory("ChunkReader"));

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkClient
} // namespace NYT
