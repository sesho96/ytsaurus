#ifndef SERIALIZE_INL_H_
#error "Direct inclusion of this file is not allowed, include serialize.h"
#endif
#undef SERIALIZE_INL_H_

#include <core/misc/serialize.h>
#include <core/misc/mpl.h>

#include <server/object_server/object.h>

#include <server/cypress_server/node.h>

#include <server/node_tracker_server/node.h>

#include <server/chunk_server/chunk.h>

namespace NYT {
namespace NCellMaster {

////////////////////////////////////////////////////////////////////////////////

struct TNonversionedObjectRefSerializer
{
    template <class T, class C>
    static void Save(C& context, T object)
    {
        typedef typename std::remove_reference<decltype(object->GetId())>::type TId;
        NYT::Save(context, object ? object->GetId() : TId());
    }

    template <class T, class C>
    static void Load(C& context, T& object)
    {
        typedef typename std::remove_reference<decltype(object->GetId())>::type TId;
        typedef typename std::remove_pointer<T>::type TObject;
        auto id = NYT::Load<TId>(context);
        object = id == TId() ? nullptr : context.template Get<TObject>(id);
    }
};

struct TNonversionedObjectRefComparer
{
    template <class T>
    static bool Compare(T* lhs, T* rhs)
    {
        return lhs->GetId() < rhs->GetId();
    }
};

////////////////////////////////////////////////////////////////////////////////

struct TVersionedObjectRefSerializer
{
    template <class T, class C>
    static void Save(C& context, T object)
    {
        typedef typename std::remove_reference<decltype(object->GetVersionedId())>::type TId;
        NYT::Save(context, object ? object->GetVersionedId() : TId());
    }

    template <class T, class C>
    static void Load(C& context, T& object)
    {
        typedef typename std::remove_reference<decltype(object->GetVersionedId())>::type TId;
        typedef typename std::remove_pointer<T>::type TObject;
        auto id = NYT::Load<TId>(context);
        object = id == TId() ? nullptr : context.template Get<TObject>(id);
    }
};

struct TVersionedObjectRefComparer
{
    template <class T>
    static bool Compare(T* lhs, T* rhs)
    {
        return lhs->GetVersionedId() < rhs->GetVersionedId();
    }
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NCellMaster
} // namespace NYT


namespace NYT {

////////////////////////////////////////////////////////////////////////////////

template <class T, class C>
struct TSerializerTraits<
    T,
    C,
    typename NMpl::TEnableIfC <
        NMpl::TAndC<
            NMpl::TIsConvertible<T, NObjectServer::TObjectBase*>::Value,
            NMpl::TNotC<
                NMpl::TIsConvertible<T, NCypressServer::TCypressNodeBase*>::Value
            >::Value
        >::Value
    >::TType
>
{
    typedef NCellMaster::TNonversionedObjectRefSerializer TSerializer;
    typedef NCellMaster::TNonversionedObjectRefComparer TComparer;
};

template <class C>
struct TSerializerTraits<NNodeTrackerServer::TNode*, C>
{
    typedef NCellMaster::TNonversionedObjectRefSerializer TSerializer;
    typedef NCellMaster::TNonversionedObjectRefComparer TComparer;
};

template <class T, class C>
struct TSerializerTraits<
    T,
    C,
    typename NMpl::TEnableIf<
        NMpl::TIsConvertible<T, NCypressServer::TCypressNodeBase*>
    >::TType
>
{
    typedef NCellMaster::TVersionedObjectRefSerializer TSerializer;
    typedef NCellMaster::TVersionedObjectRefComparer TComparer;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT

