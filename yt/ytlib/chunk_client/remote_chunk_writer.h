#pragma once

#include "chunk_writer.h"

#include "../misc/lazy_ptr.h"
#include "../misc/semaphore.h"
#include "../rpc/client.h"
#include "../chunk_holder/common.h"
#include "../chunk_holder/chunk_holder_rpc.h"
#include "../actions/action_queue.h"

#include <util/generic/deque.h>

namespace NYT
{

///////////////////////////////////////////////////////////////////////////////

class TRemoteChunkWriter
    : public IChunkWriter
{
public:
    typedef TIntrusivePtr<TRemoteChunkWriter> TPtr;
    typedef NChunkHolder::TChunkId TChunkId;
    typedef NChunkHolder::TBlockOffset TBlockOffset;

    struct TConfig
    {
        //! Number of blocks in window.
        int WindowSize;
        //! Maximum group size (in bytes).
        i64 GroupSize;
        TDuration RpcTimeout;
    };

    // Client thread
    TRemoteChunkWriter(
        const TConfig& config, 
        const TChunkId& chunkId,
        const yvector<Stroka>& nodes);

    void AddBlock(const TSharedRef& data);

    void Close();

    ~TRemoteChunkWriter();

    static Stroka GetDebugInfo();

private:

    //! A group is a bunch of blocks that is sent in a single RPC request.
    class TGroup;
    typedef TIntrusivePtr<TGroup> TGroupPtr;

    struct TNode;
    typedef TIntrusivePtr<TNode> TNodePtr;
    
    typedef ydeque<TGroupPtr> TWindow;

    typedef NChunkHolder::TChunkHolderProxy TProxy;

    USE_RPC_PROXY_METHOD(TProxy, StartChunk);
    USE_RPC_PROXY_METHOD(TProxy, FinishChunk);
    USE_RPC_PROXY_METHOD(TProxy, PutBlocks);
    USE_RPC_PROXY_METHOD(TProxy, SendBlocks);
    USE_RPC_PROXY_METHOD(TProxy, FlushBlock);

private:
    //! Manages all internal upload functionality, 
    //! sends out RPC requests, and handles responses.
    static TLazyPtr<TActionQueue> WriterThread;

    TChunkId ChunkId;

    const TConfig Config;

    DECLARE_ENUM(EWriterState,
        (Initializing)
        (Writing)
        (Failed)
    );

    //! Set in #WriterThread, read from client and writer threads
    EWriterState State;

    //! This flag is raised whenever Close is invoked.
    //! All access to this flag happens from #WriterThread.
    bool IsFinishRequested;
    TAsyncResult<TVoid>::TPtr IsFinished;

    TWindow Window;
    TSemaphore WindowSlots;

    yvector<TNodePtr> Nodes;

    //! Number of nodes that are still alive.
    int AliveNodes;

    //! A new group of blocks that is currently being filled in by the client.
    //! All access to this field happens from client thread.
    TGroupPtr CurrentGroup;

    //! Number of blocks that are already added via #AddBlock. 
    int BlockCount;
    //! The current offset inside the chunk that is being uploaded.
    TBlockOffset BlockOffset;

    /* ToDo: implement metrics

    TMetric StartChunkTiming;
    TMetric PutBlocksTiming;
    TMetric SendBlocksTiming;
    TMetric FlushBlockTiming;
    TMetric FinishChunkTiming;*/

private:
    //! Sets IsFinishRequested flag
    //! Is invoked from Close() through #WriterThread
    void RequestFinalization();
    void AddGroup(TGroupPtr group);

    void OnNodeDied(int node);

    void CheckStateAndThrow(); // client thread

    void ShiftWindow();
    TInvFlushBlock::TPtr FlushBlock(int node, int blockIndex);
    void OnFlushedBlock(int node, int blockIndex);
    void OnShiftedWindow(int blockIndex);

    void StartSession();
    TInvStartChunk::TPtr StartChunk(int node);
    void OnStartedChunk(int node);
    void OnStartedSession();

    void FinishSession();
    TInvFinishChunk::TPtr FinishChunk(int node);
    void OnFinishedChunk(int node);
    void OnFinishedSession();

    template<class TResponse>
    void CheckResponse(typename TResponse::TPtr rsp, int node, IAction::TPtr onSuccess);
};

///////////////////////////////////////////////////////////////////////////////

}

