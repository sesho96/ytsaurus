#pragma once

#include "public.h"

#include <yt/yt/core/misc/property.h>
#include <yt/yt/core/misc/guid.h>

#include <yt/yt/core/profiling/public.h>

#include <yt/yt/core/yson/string.h>

#include <yt/yt/library/tracing/public.h>

#include <library/cpp/yt/threading/spin_lock.h>

#include <atomic>

namespace NYT::NTracing {

////////////////////////////////////////////////////////////////////////////////

//! TSpanContext represents span identity propagated across the network.
//!
//! See https://opentracing.io/specification/
struct TSpanContext
{
    TTraceId TraceId = InvalidTraceId;
    TSpanId SpanId = InvalidSpanId;
    bool Sampled = false;
    bool Debug = false;
};

void FormatValue(TStringBuilderBase* builder, const TSpanContext& context, TStringBuf spec);
TString ToString(const TSpanContext& context);

////////////////////////////////////////////////////////////////////////////////

void SetGlobalTracer(const ITracerPtr& tracer);
ITracerPtr GetGlobalTracer();

////////////////////////////////////////////////////////////////////////////////

void SetTracingConfig(TTracingConfigPtr config);
TTracingConfigPtr GetTracingConfig();

////////////////////////////////////////////////////////////////////////////////

DEFINE_ENUM(ETraceContextState,
    (Disabled) // Used to propagate TraceId, RequestId and LoggingTag.
    (Recorded) // May be sampled later.
    (Sampled)  // Sampled and will be reported to jaeger.
);

////////////////////////////////////////////////////////////////////////////////

//! Accumulates information associated with a single tracing span.
/*!
 *  TTraceContext contains 3 distinct pieces of logic.
 *
 *  1) TraceId, RequestId and LoggingTag are recorded inside trace context and
 *     passed to logger.
 *  2) ElapsedCpu time is tracked by fiber scheduler during context switch.
 *  3) Opentracing compatible information is recorded and later pushed to jaeger.
 *
 *  TTraceContext objects within a single process form a tree.
 *
 *  By default, child objects inherit TraceId, RequestId and LoggingTag from the parent.
 *
 *  \note Thread affininty: any unless noted otherwise.
 */
 class TTraceContext
    : public TRefCounted
{
public:
    //! Finalizes and publishes the context (if sampling is enabled).
    /*!
     *  Safe to call multiple times from arbitrary threads; only the first call matters.
     */
    void Finish();
    bool IsFinished();

    //! IsRecorded returns a flag indicating that this trace may be sent to jaeger.
    /*!
     *  This flag should be used for fast-path optimization to skip trace annotation and child span creation.
     */
    bool IsRecorded() const;
    void SetRecorded();

    bool IsSampled() const;
    void SetSampled(bool value = true);

    //! IsPropagated returns a flag indicating that trace is serialized to proto.
    /*!
     *  By default trace context is propagated.
     *  Not thread-safe.
     */
    bool IsPropagated() const;
    void SetPropagated(bool value = true);

    TSpanContext GetSpanContext() const;
    TTraceId GetTraceId() const;
    TSpanId GetSpanId() const;
    TSpanId GetParentSpanId() const;
    bool IsDebug() const;
    const TString& GetSpanName() const;

    //! Sets request id.
    /*!
     *  Not thread-safe.
     */
    void SetRequestId(TRequestId requestId);
    TRequestId GetRequestId() const;

    //! Sets logging tag.
    /*!
     *  Not thread-safe.
     */
    void SetLoggingTag(const TString& loggingTag);
    const TString& GetLoggingTag() const;

    TInstant GetStartTime() const;

    //! Returns the wall time from the context's construction to #Finish call.
    /*!
     *  Can only be called after #Finish is complete.
     */
    TDuration GetDuration() const;

    using TTagList = TCompactVector<std::pair<TString, TString>, 4>;
    TTagList GetTags() const;

    NYson::TYsonString GetBaggage() const;
    void SetBaggage(NYson::TYsonString baggage);
    NYTree::IAttributeDictionaryPtr UnpackBaggage() const;
    NYTree::IAttributeDictionaryPtr UnpackOrCreateBaggage() const;
    void PackBaggage(const NYTree::IAttributeDictionaryPtr& baggage);

    void AddTag(const TString& tagKey, const TString& tagValue);

    template <class T>
    void AddTag(const TString& tagName, const T& tagValue);

    //! Adds error tag. Spans containing errors are highlited in Jaeger UI.
    void AddErrorTag();

    struct TTraceLogEntry
    {
        NProfiling::TCpuInstant At;
        TString Message;
    };
    using TLogList = TCompactVector<TTraceLogEntry, 4>;
    TLogList GetLogEntries() const;
    void AddLogEntry(NProfiling::TCpuInstant at, TString message);

    using TAsyncChildrenList = TCompactVector<TTraceId, 4>;
    TAsyncChildrenList GetAsyncChildren() const;
    bool AddAsyncChild(const TTraceId& traceId);

    void IncrementElapsedCpuTime(NProfiling::TCpuDuration delta);
    NProfiling::TCpuDuration GetElapsedCpuTime() const;
    TDuration GetElapsedTime() const;

    static TTraceContextPtr NewRoot(TString spanName);

    static TTraceContextPtr NewChildFromRpc(
        const NProto::TTracingExt& ext,
        TString spanName,
        TRequestId requestId = {},
        bool forceTracing = false);

    static TTraceContextPtr NewChildFromSpan(
        TSpanContext parentSpanContext,
        TString spanName,
        NYson::TYsonString baggage = NYson::TYsonString());

    TTraceContextPtr CreateChild(TString spanName);

    void AddProfilingTag(const TString& name, const TString& value);
    void AddProfilingTag(const TString& name, i64 value);
    std::vector<std::pair<TString, std::variant<TString, i64>>> GetProfilingTags();

    friend void ToProto(NProto::TTracingExt* ext, const TTraceContextPtr& context);

private:
    const TTraceId TraceId_;
    const TSpanId SpanId_;
    const TSpanId ParentSpanId_;

    // Right now, debug flag is just passed as-is. It is part of opentracing, but we do not interpret it in any way.
    const bool Debug_;

    mutable std::atomic<ETraceContextState> State_;
    bool Propagated_;

    const TTraceContextPtr ParentContext_;
    const TString SpanName_;
    TRequestId RequestId_;
    TString LoggingTag_;
    const NProfiling::TCpuInstant StartTime_;

    std::atomic<bool> Finished_ = false;
    std::atomic<bool> Submitted_ = false;
    std::atomic<NProfiling::TCpuDuration> Duration_ = {0};

    std::atomic<NProfiling::TCpuDuration> ElapsedCpuTime_ = 0;

    YT_DECLARE_SPIN_LOCK(NThreading::TSpinLock, Lock_);
    TTagList Tags_;
    TLogList Logs_;
    TAsyncChildrenList AsyncChildren_;
    NYson::TYsonString Baggage_;

    std::vector<std::pair<TString, std::variant<TString, i64>>> ProfilingTags_;

    TTraceContext(
        TSpanContext parentSpanContext,
        TString spanName,
        TTraceContextPtr parentTraceContext = nullptr);
    DECLARE_NEW_FRIEND();

    void SetDuration();
};

DEFINE_REFCOUNTED_TYPE(TTraceContext)

void FormatValue(TStringBuilderBase* builder, const TTraceContextPtr& context, TStringBuf spec);
TString ToString(const TTraceContextPtr& context);

////////////////////////////////////////////////////////////////////////////////

TTraceContext* GetCurrentTraceContext();
void FlushCurrentTraceContextTime();

//! Creates a new trace context. If the current trace context exists, it becomes the parent of the
//! created trace context.
TTraceContextPtr CreateTraceContextFromCurrent(TString spanName);

////////////////////////////////////////////////////////////////////////////////

//! Installs the given trace into the current fiber implicit trace slot.
class TCurrentTraceContextGuard
{
public:
    explicit TCurrentTraceContextGuard(TTraceContextPtr traceContext);
    TCurrentTraceContextGuard(TCurrentTraceContextGuard&& other);
    ~TCurrentTraceContextGuard();

    bool IsActive() const;
    void Release();

    const TTraceContextPtr& GetOldTraceContext() const;

private:
    bool Active_;
    TTraceContextPtr OldTraceContext_;
};

////////////////////////////////////////////////////////////////////////////////

//! Installs null trace into the current fiber implicit trace slot.
class TNullTraceContextGuard
{
public:
    TNullTraceContextGuard();
    TNullTraceContextGuard(TNullTraceContextGuard&& other);
    ~TNullTraceContextGuard();

    bool IsActive() const;
    void Release();

    const TTraceContextPtr& GetOldTraceContext() const;

private:
    bool Active_;
    TTraceContextPtr OldTraceContext_;
};

////////////////////////////////////////////////////////////////////////////////

//! Invokes TTraceContext::Finish upon destruction.
class TTraceContextFinishGuard
{
public:
    explicit TTraceContextFinishGuard(TTraceContextPtr traceContext);
    ~TTraceContextFinishGuard();

    TTraceContextFinishGuard(const TTraceContextFinishGuard&) = delete;
    TTraceContextFinishGuard(TTraceContextFinishGuard&&) = default;

    TTraceContextFinishGuard& operator=(const TTraceContextFinishGuard&) = delete;
    TTraceContextFinishGuard& operator=(TTraceContextFinishGuard&&) = default;

private:
    TTraceContextPtr TraceContext_;
};

////////////////////////////////////////////////////////////////////////////////

//! Installs the given trace into the current fiber implicit trace slot.
//! Finishes the trace context upon destruction.
class TTraceContextGuard
{
public:
    explicit TTraceContextGuard(TTraceContextPtr traceContext);
    TTraceContextGuard(TTraceContextGuard&& other) = default;

private:
    TCurrentTraceContextGuard TraceContextGuard_;
    TTraceContextFinishGuard FinishGuard_;
};

////////////////////////////////////////////////////////////////////////////////

//! Constructs a child trace context and installs it into the current fiber implicit trace slot.
//! Finishes the child trace context upon destruction.
class TChildTraceContextGuard
{
public:
    TChildTraceContextGuard(
        const TTraceContextPtr& traceContext,
        TString spanName);
    explicit TChildTraceContextGuard(
        TString spanName);
    TChildTraceContextGuard(TChildTraceContextGuard&& other) = default;

private:
    TCurrentTraceContextGuard TraceContextGuard_;
    TTraceContextFinishGuard FinishGuard_;

    static bool IsRecorded(const TTraceContextPtr& traceContext);
};

////////////////////////////////////////////////////////////////////////////////

template <class TFn>
void AnnotateTraceContext(const TFn& fn);

////////////////////////////////////////////////////////////////////////////////

class TTraceContextHandler
{
public:
    TTraceContextHandler()
        : TraceContext_(NTracing::GetCurrentTraceContext())
    { }

    NTracing::TCurrentTraceContextGuard GetTraceContextGuard()
    {
        return NTracing::TCurrentTraceContextGuard(TraceContext_);
    }

private:
    const NTracing::TTraceContextPtr TraceContext_;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTracing

#define TRACE_CONTEXT_INL_H_
#include "trace_context-inl.h"
#undef TRACE_CONTEXT_INL_H_
