﻿#pragma once

#include "public.h"
#include "key.h"

#include <ytlib/chunk_client/multi_chunk_sequential_writer.h>

#include <ytlib/misc/ref_counted.h>
#include <ytlib/misc/nullable.h>
#include <ytlib/misc/sync.h>

namespace NYT {
namespace NTableClient {

////////////////////////////////////////////////////////////////////////////////

struct ISyncWriter
    : public virtual TRefCounted
{
    virtual void Open() = 0;
    virtual void Close() = 0;

    virtual void WriteRow(const TRow& row) = 0;

    //! Returns all key columns seen so far.
    virtual const TNullable<TKeyColumns>& GetKeyColumns() const = 0;

    //! Returns the current row count (starting from 0).
    virtual i64 GetRowCount() const = 0;
};

//////////////////////////////////////////////////////////////////////////////

struct ISyncWriterUnsafe
    : public ISyncWriter
{
    virtual void WriteRowUnsafe(const TRow& row) = 0;
    virtual void WriteRowUnsafe(const TRow& row, const TNonOwningKey& key) = 0;

    virtual const std::vector<NProto::TInputChunk>& GetWrittenChunks() const = 0;

    virtual void SetProgress(double progress) = 0;
};

////////////////////////////////////////////////////////////////////////////////

template <class TChunkWriter>
class TSyncWriterAdapter
    : public ISyncWriterUnsafe
{
public:
    typedef NChunkClient::TMultiChunkSequentialWriter<TChunkWriter> TAsyncWriter;
    typedef TIntrusivePtr<TAsyncWriter> TAsyncWriterPtr;

    TSyncWriterAdapter(TAsyncWriterPtr writer)
        : Writer(writer)
    { }

    virtual void Open() override
    {
        Sync(~Writer, &TAsyncWriter::AsyncOpen);
    }

    virtual void WriteRow(const TRow& row) override
    {
        GetCurrentWriter()->WriteRow(row);
    }

    virtual void WriteRowUnsafe(const TRow& row) override
    {
        GetCurrentWriter()->WriteRowUnsafe(row);
    }

    virtual void WriteRowUnsafe(const TRow& row, const TNonOwningKey& key) override
    {
        GetCurrentWriter()->WriteRowUnsafe(row, key);
    }

    virtual void Close() override
    {
        Sync(~Writer, &TAsyncWriter::AsyncClose);
    }

    virtual const TNullable<TKeyColumns>& GetKeyColumns() const override
    {
        return Writer->GetProvider()->GetKeyColumns();
    }

    virtual i64 GetRowCount() const override
    {
        return Writer->GetProvider()->GetRowCount();
    }

    virtual const std::vector<NProto::TInputChunk>& GetWrittenChunks() const override
    {
        return Writer->GetWrittenChunks();
    }

     virtual void SetProgress(double progress)
     {
        Writer->SetProgress(progress);
     }

private:
    typename TChunkWriter::TFacade* GetCurrentWriter()
    {
        typename TChunkWriter::TFacade* facade = nullptr;

        while ((facade = Writer->GetCurrentWriter()) == nullptr) {
            Sync(~Writer, &TAsyncWriter::GetReadyEvent);
        }
        return facade;
    }

    TAsyncWriterPtr Writer;

};

////////////////////////////////////////////////////////////////////////////////

template <class TChunkWriter>
ISyncWriterUnsafePtr CreateSyncWriter(
    typename TSyncWriterAdapter<TChunkWriter>::TAsyncWriterPtr asyncWriter)
{
    return New< TSyncWriterAdapter<TChunkWriter> >(asyncWriter);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTableClient
} // namespace NYT
