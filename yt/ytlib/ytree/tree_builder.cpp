#include "stdafx.h"
#include "tree_builder.h"
#include "attributes.h"
#include "forwarding_yson_consumer.h"

#include <ytlib/actions/action_util.h>
#include <ytlib/misc/assert.h>

namespace NYT {
namespace NYTree {

////////////////////////////////////////////////////////////////////////////////

class TTreeBuilder
    : public TForwardingYsonConsumer
    , public ITreeBuilder
{
public:
    TTreeBuilder(INodeFactory* factory)
        : Factory(factory)
        , AttributeOutput(AttributeValue)
        , AttributeWriter(&AttributeOutput)
    { }


    virtual void BeginTree()
    {
        NodeStack.clear();
        NameStack.clear();
    }

    virtual INodePtr EndTree()
    {
        // Failure here means that the tree is not fully constructed yet.
        YASSERT(NodeStack.ysize() == 1);
        YASSERT(NameStack.ysize() == 0);

        auto node = NodeStack[0];
        NodeStack.clear();
        return node;
    }


    virtual void OnNode(INode* node)
    {
        PushNode(node);
    }

    virtual void OnMyStringScalar(const Stroka& value, bool hasAttributes)
    {
        UNUSED(hasAttributes);
        auto node = Factory->CreateString();
        node->SetValue(value);
        PushNode(~node);
    }

    virtual void OnMyInt64Scalar(i64 value, bool hasAttributes)
    {
        UNUSED(hasAttributes);
        auto node = Factory->CreateInt64();
        node->SetValue(value);
        PushNode(~node);
    }

    virtual void OnMyDoubleScalar(double value, bool hasAttributes)
    {
        UNUSED(hasAttributes);
        auto node = Factory->CreateDouble();
        node->SetValue(value);
        PushNode(~node);
    }

    virtual void OnMyEntity(bool hasAttributes)
    {
        UNUSED(hasAttributes);
        PushNode(~Factory->CreateEntity());
    }


    virtual void OnMyBeginList()
    {
        PushNode(~Factory->CreateList());
        PushNode(NULL);
    }

    virtual void OnMyListItem()
    {
        AddToList();
    }

    virtual void OnMyEndList(bool hasAttributes)
    {
        UNUSED(hasAttributes);
        AddToList();
    }


    virtual void OnMyBeginMap()
    {
        PushNode(~Factory->CreateMap());
        PushKey("");
        PushNode(NULL);
    }

    virtual void OnMyMapItem(const Stroka& key)
    {
        AddToMap();
        PushKey(key);
    }

    virtual void OnMyEndMap(bool hasAttributes)
    {
        UNUSED(hasAttributes);
        AddToMap();
    }


    virtual void OnMyBeginAttributes()
    { }

    virtual void OnMyAttributesItem(const Stroka& key)
    {
        AttributeKey = key;
        ForwardNode(&AttributeWriter, FromFunctor([=]
            {
                auto node = PeekNode();
                node->Attributes().SetYson(AttributeKey, AttributeValue);
                AttributeKey.clear();
                AttributeValue.clear();
            }));
    }

    virtual void OnMyEndAttributes()
    { }

private:
    INodeFactory* Factory;
    //! Contains nodes forming the current path in the tree.
    yvector<INodePtr> NodeStack;
    //! Contains names of the currently active map children.
    yvector<Stroka> NameStack;

    Stroka AttributeKey;
    TYson AttributeValue;
    TStringOutput AttributeOutput;
    TYsonWriter AttributeWriter;

    void AddToList()
    {
        auto child = PopNode();
        auto list = PeekNode()->AsList();
        if (child) {
            list->AddChild(~child);
        }
    }

    void AddToMap()
    {
        auto child = PopNode();
        auto name = PopKey();
        auto map = PeekNode()->AsMap();
        if (child) {
            YVERIFY(map->AddChild(~child, name));
        }
    }


    void PushKey(const Stroka& name)
    {
        NameStack.push_back(name);
    }

    Stroka PopKey()
    {
        YASSERT(!NameStack.empty());
        auto result = NameStack.back();
        NameStack.pop_back();
        return result;
    }


    void PushNode(INode* node)
    {
        NodeStack.push_back(node);
    }

    INodePtr PopNode()
    {
        YASSERT(!NodeStack.empty());
        auto result = NodeStack.back();
        NodeStack.pop_back();
        return result;
    }

    INodePtr PeekNode()
    {
        YASSERT(!NodeStack.empty());
        return NodeStack.back();
    }

};

TAutoPtr<ITreeBuilder> CreateBuilderFromFactory(INodeFactory* factory)
{
    return new TTreeBuilder(factory);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYTree
} // namespace NYT

