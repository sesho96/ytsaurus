#include "retry_request.h"

#include "requests.h"

#include <mapreduce/yt/common/config.h>
#include <mapreduce/yt/common/wait_proxy.h>

#include <mapreduce/yt/interface/logging/log.h>

#include <mapreduce/yt/node/node_io.h>

#include <util/string/builder.h>

namespace NYT {
namespace NDetail {

///////////////////////////////////////////////////////////////////////////////

TAttemptLimitedRetryPolicy::TAttemptLimitedRetryPolicy(ui32 attemptLimit)
    : AttemptLimit_(attemptLimit)
{ }

void TAttemptLimitedRetryPolicy::NotifyNewAttempt()
{
    ++Attempt_;
}

TMaybe<TDuration> TAttemptLimitedRetryPolicy::OnGenericError(const yexception& /*e*/)
{
    if (IsAttemptLimitExceeded()) {
        return Nothing();
    }
    return TConfig::Get()->RetryInterval;
}

TMaybe<TDuration> TAttemptLimitedRetryPolicy::OnRetriableError(const TErrorResponse& e)
{
    if (IsAttemptLimitExceeded()) {
        return Nothing();
    }
    return NYT::NDetail::GetRetryInterval(e);
}

void TAttemptLimitedRetryPolicy::OnIgnoredError(const TErrorResponse& /*e*/)
{
    --Attempt_;
}

TString TAttemptLimitedRetryPolicy::GetAttemptDescription() const
{
    return TStringBuilder() << "attempt " << Attempt_ << " of " << AttemptLimit_;
}

bool TAttemptLimitedRetryPolicy::IsAttemptLimitExceeded() const
{
    return Attempt_ >= AttemptLimit_;
}

///////////////////////////////////////////////////////////////////////////////

TResponseInfo RetryRequestWithPolicy(
    const TAuth& auth,
    THttpHeader& header,
    TStringBuf body,
    IRequestRetryPolicy* retryPolicy,
    const TRequestConfig& config)
{
    header.SetToken(auth.Token);

    bool useMutationId = header.HasMutationId();
    bool retryWithSameMutationId = false;

    TAttemptLimitedRetryPolicy defaultRetryPolicy(TConfig::Get()->RetryCount);
    if (!retryPolicy) {
        retryPolicy = &defaultRetryPolicy;
    }

    while (true) {
        retryPolicy->NotifyNewAttempt();
        THttpHeader currentHeader = header;
        TString response;

        TString requestId = "<unknown>";
        try {
            TString hostName;
            if (config.IsHeavy) {
                hostName = GetProxyForHeavyRequest(auth);
            } else {
                hostName = auth.ServerName;
            }
            THttpRequest request(hostName);
            TString requestId = request.GetRequestId();

            if (useMutationId) {
                if (retryWithSameMutationId) {
                    header.AddParameter("retry", true, /* overwrite = */ true);
                } else {
                    header.RemoveParameter("retry");
                    header.AddMutationId();
                }
            }

            request.Connect(config.SocketTimeout);
            request.SmallRequest(header, body);

            TResponseInfo result;
            result.RequestId = requestId;
            result.Response = request.GetResponse();
            return result;
        } catch (const TErrorResponse& e) {
            LogRequestError(requestId, header, e.GetError().GetMessage(), retryPolicy->GetAttemptDescription());
            retryWithSameMutationId = false;

            if (!NDetail::IsRetriable(e)) {
                throw;
            }

            auto maybeRetryTimeout = retryPolicy->OnRetriableError(e);
            if (maybeRetryTimeout) {
                TWaitProxy::Get()->Sleep(*maybeRetryTimeout);
            } else {
                throw;
            }
        } catch (const yexception& e) {
            LogRequestError(requestId, header, e.what(), retryPolicy->GetAttemptDescription());
            retryWithSameMutationId = true;

            auto maybeRetryTimeout = retryPolicy->OnGenericError(e);
            if (maybeRetryTimeout) {
                TWaitProxy::Get()->Sleep(*maybeRetryTimeout);
            } else {
                throw;
            }
        }
    }

    Y_FAIL("Retries must have either succeeded or thrown an exception");
}

static std::pair<bool,TDuration> GetRetryInfo(const TErrorResponse& errorResponse)
{
    bool retriable = true;
    TDuration retryInterval = TConfig::Get()->RetryInterval;

    int code = errorResponse.GetError().GetInnerCode();
    int httpCode = errorResponse.GetHttpCode();
    if (httpCode / 100 == 4) {
        if (httpCode == 429 || code == 904 || code == 108) {
            // request rate limit exceeded
            retryInterval = TConfig::Get()->RateLimitExceededRetryInterval;
        } else if (errorResponse.IsConcurrentOperationsLimitReached()) {
            // limit for the number of concurrent operations exceeded
            retryInterval = TConfig::Get()->StartOperationRetryInterval;
        } else if (code / 100 == 7) {
            // chunk client errors
            retryInterval = TConfig::Get()->ChunkErrorsRetryInterval;
        } else {
            retriable = false;
        }
    }
    return std::make_pair(retriable, retryInterval);
}

TDuration GetRetryInterval(const TErrorResponse& errorResponse)
{
    return GetRetryInfo(errorResponse).second;
}

bool IsRetriable(const TErrorResponse& errorResponse)
{
    return GetRetryInfo(errorResponse).first;
}

///////////////////////////////////////////////////////////////////////////////

} // namespace NDetail
} // namespace NYT
