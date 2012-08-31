#ifndef CONVERT_INL_H_
#error "Direct inclusion of this file is not allowed, include convert.h"
#endif
#undef CONVERT_INL_H_

#include "serialize.h"
#include "yson_parser.h"
#include "tree_builder.h"
#include "yson_stream.h"
#include "yson_producer.h"
#include "attribute_helpers.h"

#include <util/generic/typehelpers.h>
#include <util/generic/static_assert.h>

namespace NYT {
namespace NYTree {

////////////////////////////////////////////////////////////////////////////////

template <class T>
void Consume(const T& value, IYsonConsumer* consumer)
{
    // Check that T differs from Stroka to prevent
    // accident usage of Stroka instead of TYsonString.
    static_assert(!TSameType<T, Stroka>::Result,
        "Are you sure that you want to convert from Stroka, not from TYsonString? "
        "In this case use TRawString wrapper on Stroka.");
    
    Serialize(value, consumer);
}

////////////////////////////////////////////////////////////////////////////////

template <class T>
TYsonProducer ConvertToProducer(T&& value)
{
    auto type = GetYsonType(value);
    auto callback = BIND(
        [] (const T& value, IYsonConsumer* consumer) {
            Consume(value, consumer);
        },
        ForwardRV<T>(value));
    return TYsonProducer(callback, type);
}

template <class T>
TYsonString ConvertToYsonString(
    const T& value,
    EYsonFormat format)
{
    auto type = GetYsonType(value);
    Stroka result;
    TStringOutput stringOutput(result);
    WriteYson(&stringOutput, value, type, format);
    return TYsonString(result, type);
}

////////////////////////////////////////////////////////////////////////////////

template <class T>
INodePtr ConvertToNode(
    const T& value,
    INodeFactoryPtr factory)
{
    auto type = GetYsonType(value);
  
    auto builder = CreateBuilderFromFactory(factory);
    builder->BeginTree();

    switch (type) {
        case EYsonType::ListFragment:
            builder->OnBeginList();
            break;
        case EYsonType::MapFragment:
            builder->OnBeginMap();
            break;
        default:
            break;
    }

    Consume(value, ~builder);

    switch (type) {
        case EYsonType::ListFragment:
            builder->OnEndList();
            break;
        case EYsonType::MapFragment:
            builder->OnEndMap();
            break;
        default:
            break;
    }

    return builder->EndTree();
}

////////////////////////////////////////////////////////////////////////////////

template <class T>
TAutoPtr<IAttributeDictionary> ConvertToAttributes(const T& value)
{
    auto attributes = CreateEphemeralAttributes();
    TAttributeConsumer consumer(attributes.Get());
    Consume(value, &consumer);
    return attributes;
}

////////////////////////////////////////////////////////////////////////////////

template <class TTo>
TTo ConvertTo(INodePtr node)
{
    TTo result;
    Deserialize(result, node);
    return result;
}

template <class TTo, class T>
TTo ConvertTo(const T& value)
{
    return ConvertTo<TTo>(ConvertToNode(value));
}

////////////////////////////////////////////////////////////////////////////////

inline Stroka YsonizeString(const Stroka& string, EYsonFormat format)
{
    return ConvertToYsonString(TRawString(string), format).Data();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYTree
} // namespace NYT
