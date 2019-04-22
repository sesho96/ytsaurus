#pragma once

#include "public.h"

#include <yt/core/misc/error.h>

#include <yt/core/yson/public.h>

#include <yt/core/tracing/public.h>

namespace NYT::NHttp {

////////////////////////////////////////////////////////////////////////////////

void FillYTErrorHeaders(const IResponseWriterPtr& rsp, const TError& error);
void FillYTErrorTrailers(const IResponseWriterPtr& rsp, const TError& error);

TError ParseYTError(const IResponsePtr& rsp, bool fromTrailers = false);

//! Catches exception thrown from underlying handler body and
//! translates it into HTTP error.
IHttpHandlerPtr WrapYTException(IHttpHandlerPtr underlying);

bool MaybeHandleCors(const IRequestPtr& req, const IResponseWriterPtr& rsp, bool disableOriginCheck = false);

THashMap<TString, TString> ParseCookies(TStringBuf cookies);

void ProtectCsrfToken(const IResponseWriterPtr& rsp);

std::optional<TString> GetBalancerRequestId(const IRequestPtr& req);
std::optional<TString> GetBalancerRealIP(const IRequestPtr& req);
std::optional<TString> GetUserAgent(const IRequestPtr& req);

void ReplyJson(const IResponseWriterPtr& rsp, std::function<void(NYson::IYsonConsumer*)> producer);

NTracing::TTraceId GetTraceId(const IRequestPtr& req);
void SetTraceId(const IResponseWriterPtr& rsp, NTracing::TTraceId traceId);

NTracing::TSpanId GetSpanId(const IRequestPtr& req);

NTracing::TTraceContextPtr GetOrCreateTraceContext(const IRequestPtr& req);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NHttp
