#include "skynet_http_handler.h"

#include "local_chunk_reader.h"
#include "chunk_meta_manager.h"

#include <yt/yt/server/node/data_node/chunk_store.h>
#include <yt/yt/server/node/data_node/chunk.h>

#include <yt/yt/server/node/cluster_node/config.h>

#include <yt/yt/ytlib/chunk_client/chunk_meta_extensions.h>
#include <yt/yt/ytlib/chunk_client/chunk_reader_statistics.h>

#include <yt/yt/ytlib/table_client/schemaless_multi_chunk_reader.h>
#include <yt/yt/client/table_client/name_table.h>
#include <yt/yt/ytlib/table_client/chunk_state.h>
#include <yt/yt/ytlib/table_client/columnar_chunk_meta.h>
#include <yt/yt/ytlib/table_client/helpers.h>

#include <yt/yt/ytlib/api/native/table_reader.h>

#include <yt/yt/client/table_client/blob_reader.h>

#include <yt/yt/core/concurrency/async_stream.h>

#include <yt/yt/core/http/http.h>

#include <library/cpp/cgiparam/cgiparam.h>

namespace NYT::NDataNode {

using namespace NHttp;
using namespace NApi;
using namespace NChunkClient;
using namespace NChunkClient;
using namespace NTableClient;
using namespace NClusterNode;
using namespace NConcurrency;

using NChunkClient::NProto::TMiscExt;

////////////////////////////////////////////////////////////////////////////////

const NLogging::TLogger Logger("SkynetHandler");

////////////////////////////////////////////////////////////////////////////////

void ParseRequest(TStringBuf rawQuery, TChunkId* chunkId, TReadRange* readRange, i64* partIndex)
{
    TCgiParameters params(rawQuery);

    if (!params.Has("chunk_id")) {
        THROW_ERROR_EXCEPTION("Missing paramenter \"chunk_id\" in URL query string.");
    }

    *chunkId = TChunkId::FromString(params.Get("chunk_id"));

    if (!params.Has("lower_row_index")) {
        THROW_ERROR_EXCEPTION("Missing paramenter \"lower_row_index\" in URL query string.");
    }
    if (!params.Has("upper_row_index")) {
        THROW_ERROR_EXCEPTION("Missing paramenter \"upper_row_index\" in URL query string.");
    }

    readRange->LowerLimit().SetRowIndex(FromString<i64>(params.Get("lower_row_index")));
    readRange->UpperLimit().SetRowIndex(FromString<i64>(params.Get("upper_row_index")));

    if (!params.Has("start_part_index")) {
        THROW_ERROR_EXCEPTION("Missing paramenter \"start_part_index\" in URL query string.");
    }

    *partIndex = FromString<i64>(params.Get("start_part_index"));

    if (*partIndex < 0 || readRange->LowerLimit().GetRowIndex() < 0 || readRange->UpperLimit().GetRowIndex() < 0) {
        THROW_ERROR_EXCEPTION("Parameter is negative")
            << TErrorAttribute("part_index", *partIndex)
            << TErrorAttribute("read_range", *readRange);
    }
}

////////////////////////////////////////////////////////////////////////////////

class TSkynetHttpHandler
    : public IHttpHandler
{
public:
    explicit TSkynetHttpHandler(TBootstrap* bootstrap)
        : Bootstrap_(bootstrap)
    { }

    virtual void HandleRequest(const IRequestPtr& req, const IResponseWriterPtr& rsp) override
    {
        TChunkId chunkId;
        TReadRange readRange;
        i64 startPartIndex;
        ParseRequest(req->GetUrl().RawQuery, &chunkId, &readRange, &startPartIndex);

        auto chunk = Bootstrap_->GetChunkStore()->GetChunkOrThrow(chunkId, AllMediaIndex);

        TWorkloadDescriptor skynetWorkload(EWorkloadCategory::UserBatch);
        skynetWorkload.Annotations = {"skynet"};

        static std::vector<int> miscExtension = {
            TProtoExtensionTag<TMiscExt>::Value
        };

        TChunkReadOptions chunkReadOptions;
        chunkReadOptions.WorkloadDescriptor = skynetWorkload;
        chunkReadOptions.ChunkReaderStatistics = New<TChunkReaderStatistics>();
        chunkReadOptions.ReadSessionId = TReadSessionId::Create();

        auto chunkMeta = WaitFor(chunk->ReadMeta(chunkReadOptions))
            .ValueOrThrow();

        auto miscExt = GetProtoExtension<TMiscExt>(chunkMeta->extensions());
        if (!miscExt.shared_to_skynet()) {
            THROW_ERROR_EXCEPTION("Chunk access not allowed")
                << TErrorAttribute("chunk_id", chunkId);
        }
        if (readRange.LowerLimit().GetRowIndex() >= miscExt.row_count() ||
            readRange.UpperLimit().GetRowIndex() >= miscExt.row_count() + 1 ||
            readRange.LowerLimit().GetRowIndex() >= readRange.UpperLimit().GetRowIndex())
        {
            THROW_ERROR_EXCEPTION("Requested rows are out of bound")
                << TErrorAttribute("read_range", readRange)
                << TErrorAttribute("row_count", miscExt.row_count());
        }

        auto readerConfig = New<TReplicationReaderConfig>();
        auto chunkReader = CreateLocalChunkReader(
            readerConfig,
            chunk,
            Bootstrap_->GetChunkBlockManager(),
            Bootstrap_->GetBlockCache(),
            Bootstrap_->GetChunkMetaManager()->GetBlockMetaCache());

        auto chunkState = New<TChunkState>(Bootstrap_->GetBlockCache());

        auto schemalessReader = CreateSchemalessRangeChunkReader(
            chunkState,
            New<TColumnarChunkMeta>(*chunkMeta),
            New<TChunkReaderConfig>(),
            New<TChunkReaderOptions>(),
            chunkReader,
            New<TNameTable>(),
            chunkReadOptions,
            /* sortColumns */ {},
            /* omittedInaccessibleColumns */ {},
            /* columnFilter */ {},
            readRange);

        auto apiReader = CreateApiFromSchemalessChunkReaderAdapter(std::move(schemalessReader));

        auto blobReader = NTableClient::CreateBlobTableReader(
            apiReader,
            TString("part_index"),
            TString("data"),
            startPartIndex);

        rsp->SetStatus(EStatusCode::OK);

        const auto& throttler = Bootstrap_->GetDataNodeThrottler(NDataNode::EDataNodeThrottlerKind::SkynetOut);
        while (true) {
            auto blob = WaitFor(blobReader->Read())
                .ValueOrThrow();

            if (blob.Empty()) {
                break;
            }

            WaitFor(throttler->Throttle(blob.Size()))
                .ThrowOnError();

            WaitFor(rsp->Write(blob))
                .ThrowOnError();
        }

        WaitFor(rsp->Close())
            .ThrowOnError();
    }

private:
    TBootstrap* Bootstrap_;
};

IHttpHandlerPtr MakeSkynetHttpHandler(TBootstrap* bootstrap)
{
    return New<TSkynetHttpHandler>(bootstrap);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NDataNode
