#pragma once

#include <core/misc/common.h>
#include <core/misc/enum.h>

#include <ytlib/transaction_client/public.h>

#include <ytlib/table_client/public.h>

namespace NYT {
namespace NVersionedTableClient {

////////////////////////////////////////////////////////////////////////////////

using NTransactionClient::TTimestamp;
using NTransactionClient::NullTimestamp;
using NTransactionClient::MinTimestamp;
using NTransactionClient::MaxTimestamp;

using NTableClient::TKeyColumns;

////////////////////////////////////////////////////////////////////////////////

DECLARE_ENUM(ERowsetType,
    (Simple)
    (Versioned)   // With timestamps and tombstones
);

DECLARE_ENUM(EValueType,
    ((Min)         (0))
    ((TheBottom)   (1))
    ((Null)        (2))
    ((Integer)     (3))
    ((Double)      (4))
    ((String)      (5))
    ((Any)         (6))
    ((Max)        (64))
);

static const int TypicalColumnCount = 64;

////////////////////////////////////////////////////////////////////////////////

struct TUnversionedValue;
struct TVersionedValue;
struct TRowHeader;

template <class TValue>
class TRow;
typedef TRow<TVersionedValue>   TVersionedRow;
typedef TRow<TUnversionedValue> TUnversionedRow;

template <class TValue>
class TOwningRow;
typedef TOwningRow<TVersionedValue>   TVersionedOwningRow;
typedef TOwningRow<TUnversionedValue> TUnversionedOwningRow;

typedef TUnversionedOwningRow TOwningKey;
typedef TUnversionedRow       TKey;

template <class TValue>
class TRowBuilder;
typedef TRowBuilder<TVersionedValue>   TVersionedRowBuilder;
typedef TRowBuilder<TUnversionedValue> TUnversionedRowBuilder;

struct TColumnSchema;
class TTableSchema;

class TNameTable;
typedef TIntrusivePtr<TNameTable> TNameTablePtr;

class TBlockWriter;

class TChunkWriter;
typedef TIntrusivePtr<TChunkWriter> TChunkWriterPtr;

struct IReader;
typedef TIntrusivePtr<IReader> IReaderPtr;

struct IWriter;
typedef TIntrusivePtr<IWriter> IWriterPtr;

class TChunkWriterConfig;
typedef TIntrusivePtr<TChunkWriterConfig> TChunkWriterConfigPtr;

class TChunkReaderConfig;
typedef TIntrusivePtr<TChunkReaderConfig> TChunkReaderConfigPtr;

////////////////////////////////////////////////////////////////////////////////

} // namespace NVersionedTableClient
} // namespace NYT
