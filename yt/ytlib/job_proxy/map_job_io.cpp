#include "stdafx.h"

#include "map_job_io.h"
#include "table_output.h"
#include "config.h"
#include "stderr_output.h"

// ToDo(psushin): use public.h everywhere.
#include <ytlib/chunk_client/client_block_cache.h>
#include <ytlib/table_client/chunk_sequence_reader.h>
#include <ytlib/table_client/table_chunk_sequence_writer.h>
#include <ytlib/table_client/sync_writer.h>
#include <ytlib/table_client/sync_reader.h>
#include <ytlib/table_client/table_producer.h>
#include <ytlib/table_client/table_consumer.h>
#include <ytlib/table_client/schema.h>

namespace NYT {
namespace NJobProxy {

////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger = JobProxyLogger;

////////////////////////////////////////////////////////////////////

using namespace NScheduler;
using namespace NScheduler::NProto;
using namespace NTableClient;
using namespace NYTree;
using namespace NTransactionClient;
using namespace NChunkClient;
using namespace NChunkServer;

////////////////////////////////////////////////////////////////////

TMapJobIO::TMapJobIO(
    TJobIOConfigPtr config,
    NRpc::IChannelPtr masterChannel,
    const NScheduler::NProto::TJobSpec& jobSpec)
    : Config(config)
    , MasterChannel(masterChannel)
    , JobSpec(jobSpec)
{
    YCHECK(JobSpec.input_specs_size() == 1);
}

int TMapJobIO::GetInputCount() const 
{
    // Always single input for map.
    return 1;
}

int TMapJobIO::GetOutputCount() const
{
    return JobSpec.output_specs_size();
}

TAutoPtr<NTableClient::TTableProducer> 
TMapJobIO::CreateTableInput(int index, NYTree::IYsonConsumer* consumer) const
{
    YASSERT(index < GetInputCount());

    auto blockCache = CreateClientBlockCache(~New<TClientBlockCacheConfig>());

    std::vector<NTableClient::NProto::TInputChunk> chunks(
        JobSpec.input_specs(0).chunks().begin(),
        JobSpec.input_specs(0).chunks().end());

    LOG_DEBUG("Opening input %d with %d chunks", 
        index, 
        static_cast<int>(chunks.size()));

    auto reader = New<TChunkSequenceReader>(
        Config->ChunkSequenceReader,
        MasterChannel,
        blockCache,
        chunks);
    auto syncReader = New<TSyncReaderAdapter>(reader);
    syncReader->Open();

    return new TTableProducer(syncReader, consumer);
}

NTableClient::ISyncWriterPtr TMapJobIO::CreateTableOutput(int index) const
{
    YASSERT(index < GetOutputCount());
    const TYson& channels = JobSpec.output_specs(index).channels();
    YASSERT(!channels.empty());

    auto chunkSequenceWriter = New<TTableChunkSequenceWriter>(
        Config->ChunkSequenceWriter,
        MasterChannel,
        TTransactionId::FromProto(JobSpec.output_transaction_id()),
        TChunkListId::FromProto(JobSpec.output_specs(index).chunk_list_id()),
        ChannelsFromYson(channels));

    auto syncWriter = CreateSyncWriter(chunkSequenceWriter);
    syncWriter->Open();

    return syncWriter;
}

void TMapJobIO::UpdateProgress()
{
    YUNIMPLEMENTED();
}

double TMapJobIO::GetProgress() const
{
    YUNIMPLEMENTED();
}

TAutoPtr<TErrorOutput> TMapJobIO::CreateErrorOutput() const
{
    return new TErrorOutput(
        Config->ErrorFileWriter, 
        MasterChannel, 
        TTransactionId::FromProto(JobSpec.output_transaction_id()));
}

////////////////////////////////////////////////////////////////////

} // namespace NJobProxy
} // namespace NYT
