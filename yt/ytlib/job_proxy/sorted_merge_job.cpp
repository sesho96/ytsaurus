﻿#include "stdafx.h"
#include "sorted_merge_job.h"
#include "private.h"
#include "config.h"

#include <ytlib/object_server/id.h>
#include <ytlib/election/leader_channel.h>
#include <ytlib/chunk_client/async_reader.h>
#include <ytlib/chunk_client/remote_reader.h>
#include <ytlib/chunk_client/client_block_cache.h>
#include <ytlib/chunk_server/public.h>
#include <ytlib/table_client/sync_writer.h>
#include <ytlib/table_client/private.h>
#include <ytlib/table_client/table_chunk_sequence_writer.h>
#include <ytlib/table_client/table_chunk_sequence_reader.h>
#include <ytlib/table_client/merging_reader.h>
#include <ytlib/ytree/yson_string.h>

namespace NYT {
namespace NJobProxy {

using namespace NElection;
using namespace NTableClient;
using namespace NChunkClient;
using namespace NChunkServer;
using namespace NObjectServer;
using namespace NScheduler::NProto;

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger = JobProxyLogger;
static NProfiling::TProfiler& Profiler = JobProxyProfiler;

////////////////////////////////////////////////////////////////////////////////

TSortedMergeJob::TSortedMergeJob(
    TJobProxyConfigPtr proxyConfig,
    const TJobSpec& jobSpec)
{
    YCHECK(jobSpec.output_specs_size() == 1);

    auto blockCache = CreateClientBlockCache(New<TClientBlockCacheConfig>());
    auto masterChannel = CreateLeaderChannel(proxyConfig->Masters);

    {
        std::vector<TTableChunkSequenceReaderPtr> readers;
        TReaderOptions options;
        options.ReadKey = true;

        FOREACH (const auto& inputSpec, jobSpec.input_specs()) {
            // ToDo(psushin): validate that input chunks are sorted.
            std::vector<NTableClient::NProto::TInputChunk> chunks(
                inputSpec.chunks().begin(),
                inputSpec.chunks().end());

            auto reader = New<TTableChunkSequenceReader>(
                proxyConfig->JobIO->ChunkSequenceReader,
                masterChannel,
                blockCache,
                MoveRV(chunks),
                options);

            readers.push_back(reader);
        }

        Reader = New<TMergingReader>(readers);
    }

    {
        const auto& mergeSpec = jobSpec.GetExtension(TMergeJobSpecExt::merge_job_spec_ext); 

        // ToDo(psushin): estimate row count for writer.
        Writer = New<TTableChunkSequenceWriter>(
            proxyConfig->JobIO->ChunkSequenceWriter,
            ~masterChannel,
            TTransactionId::FromProto(jobSpec.output_transaction_id()),
            TChunkListId::FromProto(jobSpec.output_specs(0).chunk_list_id()),
            ChannelsFromYson(NYTree::TYsonString(jobSpec.output_specs(0).channels())),
            FromProto<Stroka>(mergeSpec.key_columns()));
    }
}

TJobResult TSortedMergeJob::Run()
{
    PROFILE_TIMING ("/sorted_merge_time") {
        auto writer = CreateSyncWriter(Writer);

        // Open readers, remove invalid ones, and create the initial heap.
        LOG_INFO("Initializing");
        {
            Reader->Open();
            writer->Open();
        }
        PROFILE_TIMING_CHECKPOINT("init");

        // Run the actual merge.
        LOG_INFO("Merging");
        while (Reader->IsValid()) {
            writer->WriteRowUnsafe(Reader->GetRow(), Reader->GetKey());
            Reader->NextRow();
        }
        PROFILE_TIMING_CHECKPOINT("merge");

        LOG_INFO("Finalizing");
        {
            writer->Close();

            TJobResult result;
            *result.mutable_error() = TError().ToProto();
            return result;
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NJobProxy
} // namespace NYT
