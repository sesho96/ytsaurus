#pragma once

#include "yson_consumer.h"

#include <contrib/testing/framework.h>

namespace NYT {
namespace NYTree {

////////////////////////////////////////////////////////////////////////////////

class TMockYsonConsumer
    : public TYsonConsumerBase
{
public:
    MOCK_METHOD1(OnStringScalar, void(const TStringBuf& value));
    MOCK_METHOD1(OnIntegerScalar, void(i64 value));
    MOCK_METHOD1(OnDoubleScalar, void(double value));
    MOCK_METHOD0(OnEntity, void());

    MOCK_METHOD0(OnBeginList, void());
    MOCK_METHOD0(OnListItem, void());
    MOCK_METHOD0(OnEndList, void());

    MOCK_METHOD0(OnBeginMap, void());
    MOCK_METHOD1(OnKeyedItem, void(const TStringBuf& name));
    MOCK_METHOD0(OnEndMap, void());

    MOCK_METHOD0(OnBeginAttributes, void());
    MOCK_METHOD0(OnEndAttributes, void());
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYTree
} // namespace NYT
