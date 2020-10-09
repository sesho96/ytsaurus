#include "table_writer.h"
#include "helpers.h"
#include "row_stream.h"
#include "wire_row_stream.h"

#include <yt/client/api/table_writer.h>

#include <yt/client/table_client/name_table.h>
#include <yt/client/table_client/schema.h>
#include <yt/client/table_client/unversioned_row_batch.h>

#include <yt/core/rpc/stream.h>

namespace NYT::NApi::NRpcProxy {

using namespace NConcurrency;
using namespace NTableClient;

////////////////////////////////////////////////////////////////////////////////

class TTableWriter
    : public ITableWriter
{
public:
    TTableWriter(
        IAsyncZeroCopyOutputStreamPtr underlying,
        TTableSchemaPtr schema)
        : Underlying_(std::move(underlying))
        , Schema_(std::move(schema))
        , Encoder_(CreateWireRowStreamEncoder(NameTable_))
    {
        YT_VERIFY(Underlying_);
        NameTable_->SetEnableColumnNameValidation();
    }

    virtual bool Write(TRange<TUnversionedRow> rows) override
    {
        YT_VERIFY(!Closed_);
        YT_VERIFY(ReadyEvent_.IsSet() && ReadyEvent_.Get().IsOK());

        auto batch = CreateBatchFromUnversionedRows(TSharedRange<TUnversionedRow>(rows, nullptr));

        auto block = Encoder_->Encode(batch, nullptr);

        ReadyEvent_ = NewPromise<void>();
        ReadyEvent_.TrySetFrom(Underlying_->Write(std::move(block)));

        return ReadyEvent_.IsSet() && ReadyEvent_.Get().IsOK();
    }

    virtual TFuture<void> GetReadyEvent() override
    {
        return ReadyEvent_;
    }

    virtual TFuture<void> Close() override
    {
        YT_VERIFY(!Closed_);
        Closed_ = true;

        return Underlying_->Close();
    }

    virtual const TNameTablePtr& GetNameTable() const override
    {
        return NameTable_;
    }

    virtual const TTableSchemaPtr& GetSchema() const override
    {
        return Schema_;
    }

private:
    const IAsyncZeroCopyOutputStreamPtr Underlying_;
    const TTableSchemaPtr Schema_;

    const TNameTablePtr NameTable_ = New<TNameTable>();
    const IRowStreamEncoderPtr Encoder_;

    TPromise<void> ReadyEvent_ = MakePromise<void>(TError());
    bool Closed_ = false;
};

TFuture<ITableWriterPtr> CreateTableWriter(
    TApiServiceProxy::TReqWriteTablePtr request)
{
    auto schema = New<TTableSchema>();
    return NRpc::CreateRpcClientOutputStream(
        std::move(request),
        BIND ([=] (const TSharedRef& metaRef) {
            NApi::NRpcProxy::NProto::TWriteTableMeta meta;
            if (!TryDeserializeProto(&meta, metaRef)) {
                THROW_ERROR_EXCEPTION("Failed to deserialize schema for table writer");
            }

            FromProto(schema.Get(), meta.schema());
        }))
        .Apply(BIND([=] (const IAsyncZeroCopyOutputStreamPtr& outputStream) {
            return New<TTableWriter>(outputStream, std::move(schema));
        })).As<ITableWriterPtr>();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NApi::NRpcProxy
