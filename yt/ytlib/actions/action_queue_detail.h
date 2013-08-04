#pragma once

#include "common.h"
#include "invoker.h"
#include "callback.h"

#include <ytlib/misc/thread.h>
#include <ytlib/misc/event_count.h>

#include <ytlib/profiling/profiler.h>

#include <ytlib/ypath/public.h>

#include <util/system/thread.h>
#include <util/system/event.h>

#include <util/thread/lfqueue.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

class TInvokerQueue;
typedef TIntrusivePtr<TInvokerQueue> TInvokerQueuePtr;

class TExecutorThread;
typedef TIntrusivePtr<TExecutorThread> TExecutorThreadPtr;

class TSingleQueueExecutorThread;
typedef TIntrusivePtr<TSingleQueueExecutorThread> TSingleQueueExecutorThreadPtr;

////////////////////////////////////////////////////////////////////////////////

DECLARE_ENUM(EBeginExecuteResult,
    (Success)
    (QueueEmpty)
    (LoopTerminated)
);

struct TEnqueuedAction
{
    NProfiling::TCpuInstant EnqueueInstant;
    NProfiling::TCpuInstant StartInstant;
    TClosure Callback;
};

class TInvokerQueue
    : public IInvoker
{
public:
    TInvokerQueue(
        TEventCount* eventCount,
        IInvoker* currentInvoker,
        const NProfiling::TTagIdList& tagIds,
        bool enableLogging,
        bool enableProfiling);

    bool Invoke(const TClosure& callback);
    void Shutdown();

    EBeginExecuteResult BeginExecute(TEnqueuedAction* action);
    void EndExecute(TEnqueuedAction* action);

    int GetSize() const;
    bool IsEmpty() const;

private:
    TEventCount* EventCount;
    IInvoker* CurrentInvoker;
    bool EnableLogging;

    bool Running;

    NProfiling::TProfiler Profiler;

    NProfiling::TRateCounter EnqueueCounter;
    NProfiling::TRateCounter DequeueCounter;
    TAtomic QueueSize;
    NProfiling::TAggregateCounter QueueSizeCounter;
    NProfiling::TAggregateCounter WaitTimeCounter;
    NProfiling::TAggregateCounter ExecTimeCounter;
    NProfiling::TAggregateCounter TotalTimeCounter;

    TLockFreeQueue<TEnqueuedAction> Queue;

};

///////////////////////////////////////////////////////////////////////////////

class TExecutorThread
    : public TRefCounted
{
public:
    virtual ~TExecutorThread();
    
    void Start();
    void Shutdown();

    bool IsRunning() const;

protected:
    TExecutorThread(
        TEventCount* eventCount,
        const Stroka& threadName,
        const NProfiling::TTagIdList& tagIds,
        bool enableLogging,
        bool enableProfiling);

    virtual EBeginExecuteResult BeginExecute() = 0;
    virtual void EndExecute() = 0;
    
    virtual void OnThreadStart();
    virtual void OnThreadShutdown();

private:
    friend class TInvokerQueue;

    static void* ThreadMain(void* opaque);
    void ThreadMain();
    void FiberMain();

    EBeginExecuteResult Execute();

    TEventCount* EventCount;
    Stroka ThreadName;
    bool EnableLogging;

    NProfiling::TProfiler Profiler;

    volatile bool Running;
    int FibersCreated;
    int FibersAlive;

    NThread::TThreadId ThreadId;
    TThread Thread;
    
};

////////////////////////////////////////////////////////////////////////////////

class TSingleQueueExecutorThread
    : public TExecutorThread
{
public:
    TSingleQueueExecutorThread(
        TInvokerQueuePtr queue,
        TEventCount* eventCount,
        const Stroka& threadName,
        const NProfiling::TTagIdList& tagIds,
        bool enableLogging,
        bool enableProfiling);

    ~TSingleQueueExecutorThread();

    IInvokerPtr GetInvoker();

protected:
    TInvokerQueuePtr Queue;

    TEnqueuedAction CurrentAction;

    virtual EBeginExecuteResult BeginExecute() override;
    virtual void EndExecute() override;

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT

