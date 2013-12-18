#pragma once

#include "public.h"

#include <ytlib/new_table_client/unversioned_row.h>

namespace NYT {
namespace NTabletNode {

////////////////////////////////////////////////////////////////////////////////

struct TStaticRowHeader
{
    NVersionedTableClient::TTimestamp LastCommitTimestamp;
    
    // Variable-size part:
    // * TUnversionedValue per each key column
    // * TTimestamp* for timestamps
    // * TVersionedValue* per each fixed non-key column
    // * ui16 for timestamp list size
    // * ui16 per each fixed non-key column for list size
    // * padding up to 8 bytes
};

////////////////////////////////////////////////////////////////////////////////

class TStaticRow
{
public:
    TStaticRow()
        : Header_(nullptr)
    { }

    explicit TStaticRow(TStaticRowHeader* header)
        : Header_(header)
    { }


    static size_t GetSize(int keyCount, int schemaColumnCount)
    {
        size_t size =
            sizeof (TStaticRowHeader) +
            sizeof (NVersionedTableClient::TUnversionedValue) * keyCount +
            sizeof (TTimestamp*) +
            sizeof (NVersionedTableClient::TVersionedValue*) * (schemaColumnCount - keyCount) +
            sizeof (ui16) +
            sizeof (ui16) * (schemaColumnCount - keyCount);
        return (size + 7) & ~7;
    }


    explicit operator bool() const
    {
        return Header_ != nullptr;
    }

    
    TTimestamp GetLastCommitTimestamp() const
    {
        return Header_->LastCommitTimestamp;
    }

    void SetLastCommitTimestamp(TTimestamp timestamp)
    {
        Header_->LastCommitTimestamp = timestamp;
    }


    const NVersionedTableClient::TUnversionedValue& operator [](int id) const
    {
        return GetKeys()[id];
    }

    NVersionedTableClient::TUnversionedValue* GetKeys() const
    {
        return reinterpret_cast<NVersionedTableClient::TUnversionedValue*>(
            reinterpret_cast<char*>(Header_) +
            sizeof(TStaticRowHeader));
    }


    TTimestamp* GetTimestamps(int keyCount) const
    {
        return *GetTimestampsPtr(keyCount);
    }

    void SetTimestamps(int keyCount, TTimestamp* timestamps)
    {
        *GetTimestampsPtr(keyCount) = timestamps;
    }


    int GetTimestampCount(int keyCount, int schemaColumnCount) const
    {
        return *GetTimestampCountPtr(keyCount, schemaColumnCount);
    }

    void SetTimestampCount(int keyCount, int schemaColumnCount, int count) const
    {
        *GetTimestampCountPtr(keyCount, schemaColumnCount) = count;
    }


    NVersionedTableClient::TVersionedValue* GetFixedValues(int index, int keyCount) const
    {
        return *GetFixedValuesPtr(index, keyCount);
    }

    void SetFixedValues(int keyCount, int index, NVersionedTableClient::TVersionedValue* values)
    {
        *GetFixedValuesPtr(index, keyCount) = values;
    }


    int GetFixedValueCount(int index, int keyCount, int schemaColumnCount) const
    {
        return *GetFixedValueCountPtr(index, keyCount, schemaColumnCount);
    }

    void SetFixedValueCount(int index, int count, int keyCount, int schemaColumnCount)
    {
        *GetFixedValueCountPtr(index, keyCount, schemaColumnCount) = count;
    }

private:
    TStaticRowHeader* Header_;

    TTimestamp** GetTimestampsPtr(int keyCount) const
    {
        return reinterpret_cast<TTimestamp**>(
            reinterpret_cast<char*>(Header_) +
            sizeof(TStaticRowHeader) +
            sizeof(NVersionedTableClient::TUnversionedValue) * keyCount);
    }

    ui16* GetTimestampCountPtr(int keyCount, int schemaColumnCount) const
    {
        return reinterpret_cast<ui16*>(
            reinterpret_cast<char*>(Header_) +
            sizeof(TStaticRowHeader) +
            sizeof(NVersionedTableClient::TUnversionedValue) * keyCount +
            sizeof(TTimestamp*) +
            sizeof(NVersionedTableClient::TVersionedValue*) * (schemaColumnCount - keyCount));
    }

    NVersionedTableClient::TVersionedValue** GetFixedValuesPtr(int index, int keyCount) const
    {
        return reinterpret_cast<NVersionedTableClient::TVersionedValue**>(
            reinterpret_cast<char*>(Header_) +
            sizeof(TStaticRowHeader) +
            sizeof(NVersionedTableClient::TUnversionedValue) * keyCount +
            sizeof(TTimestamp*) +
            sizeof(NVersionedTableClient::TVersionedValue*) * index);
    }

    ui16* GetFixedValueCountPtr(int index, int keyCount, int schemaColumnCount) const
    {
        return reinterpret_cast<ui16*>(
            reinterpret_cast<char*>(Header_) +
            sizeof(TStaticRowHeader) +
            sizeof(NVersionedTableClient::TUnversionedValue) * keyCount +
            sizeof(TTimestamp*) +
            sizeof(NVersionedTableClient::TVersionedValue*) * (schemaColumnCount - keyCount) +
            sizeof(ui16) +
            sizeof(ui16) * index);
    }

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletNode
} // namespace NYT
