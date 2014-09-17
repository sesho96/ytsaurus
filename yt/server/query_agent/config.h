#pragma once

#include "public.h"

#include <core/ytree/yson_serializable.h>

#include <core/compression/public.h>

namespace NYT {
namespace NQueryAgent {

////////////////////////////////////////////////////////////////////////////////

class TQueryAgentConfig
    : public NYTree::TYsonSerializable
{
public:
    int ThreadPoolSize;
    int MaxConcurrentRequests;
    int MaxSubsplitsPerTablet;
    int MaxQueryRetries;

    NCompression::ECodec SelectResponseCodec;

    TQueryAgentConfig()
    {
        RegisterParameter("thread_pool_size", ThreadPoolSize)
            .GreaterThan(0)
            .Default(4);
        RegisterParameter("max_concurrent_requests", MaxConcurrentRequests)
            .GreaterThan(0)
            .Default(4);
        RegisterParameter("max_subsplits_per_tablet", MaxSubsplitsPerTablet)
            .GreaterThan(0)
            .Default(4);
        RegisterParameter("max_query_retries", MaxQueryRetries)
            .GreaterThanOrEqual(1)
            .Default(10);

        RegisterParameter("select_response_codec", SelectResponseCodec)
            .Default(NCompression::ECodec::Lz4);
    }
};

DEFINE_REFCOUNTED_TYPE(TQueryAgentConfig)

////////////////////////////////////////////////////////////////////////////////

} // namespace NQueryAgent
} // namespace NYT

