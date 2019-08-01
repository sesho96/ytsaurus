#include "storage_distributor.h"

#include "config.h"
#include "bootstrap.h"
#include "block_input_stream.h"
#include "type_helpers.h"
#include "helpers.h"
#include "query_context.h"
#include "subquery.h"
#include "join_workaround.h"
#include "table.h"
#include "db_helpers.h"
#include "block_output_stream.h"
#include "query_helpers.h"

#include <yt/ytlib/chunk_client/input_data_slice.h>

#include <yt/client/ypath/rich.h>

#include <yt/ytlib/api/native/client.h>

#include <yt/ytlib/table_client/schemaless_chunk_writer.h>
#include <yt/client/table_client/name_table.h>

#include <DataStreams/materializeBlock.h>
#include <DataStreams/MaterializingBlockInputStream.h>
#include <DataStreams/RemoteBlockInputStream.h>
#include <Interpreters/InterpreterSelectQuery.h>
#include <DataTypes/DataTypeFactory.h>
#include <DataTypes/DataTypeNullable.h>
#include <Storages/MergeTree/KeyCondition.h>
#include <Storages/MergeTree/MergeTreeData.h>
#include <Storages/StorageFactory.h>
#include <Parsers/ASTFunction.h>
#include <Parsers/ASTLiteral.h>
#include <Parsers/ASTIdentifier.h>
#include <Parsers/ASTSelectQuery.h>
#include <Parsers/ASTCreateQuery.h>
#include <Parsers/ASTSampleRatio.h>
#include <Parsers/ASTTablesInSelectQuery.h>
#include <Parsers/queryToString.h>

#include <library/string_utils/base64/base64.h>

namespace NYT::NClickHouseServer {

using namespace NYPath;
using namespace NTableClient;
using namespace NYson;
using namespace NYTree;

/////////////////////////////////////////////////////////////////////////////

DB::Settings PrepareLeafJobSettings(const DB::Settings& settings)
{
    auto newSettings = settings;

    newSettings.queue_max_wait_ms = DB::Cluster::saturate(
        newSettings.queue_max_wait_ms,
        settings.max_execution_time);

    // Does not matter on remote servers, because queries are sent under different user.
    newSettings.max_concurrent_queries_for_user = 0;
    newSettings.max_memory_usage_for_user = 0;

    // This setting is really not for user and should not be sent to remote server.
    newSettings.max_memory_usage_for_all_queries = 0;

    // Set as unchanged to avoid sending to remote server.
    newSettings.max_concurrent_queries_for_user.changed = false;
    newSettings.max_memory_usage_for_user.changed = false;
    newSettings.max_memory_usage_for_all_queries.changed = false;

    newSettings.max_query_size = 0;

    return newSettings;
}

DB::ThrottlerPtr CreateNetThrottler(const DB::Settings& settings)
{
    DB::ThrottlerPtr throttler;
    if (settings.max_network_bandwidth || settings.max_network_bytes) {
        throttler = std::make_shared<DB::Throttler>(
            settings.max_network_bandwidth,
            settings.max_network_bytes,
            "Limit for bytes to send or receive over network exceeded.");
    }
    return throttler;
}

DB::BlockInputStreamPtr CreateLocalStream(
    const DB::ASTPtr& queryAst,
    const DB::Context& context,
    DB::QueryProcessingStage::Enum processedStage)
{
    DB::InterpreterSelectQuery interpreter(queryAst, context, DB::SelectQueryOptions(processedStage));
    DB::BlockInputStreamPtr stream = interpreter.execute().in;

    // Materialization is needed, since from remote servers the constants come materialized.
    // If you do not do this, different types (Const and non-Const) columns will be produced in different threads,
    // And this is not allowed, since all code is based on the assumption that in the block stream all types are the same.
    return std::make_shared<DB::MaterializingBlockInputStream>(stream);
}

DB::BlockInputStreamPtr CreateRemoteStream(
    const IClusterNodePtr& remoteNode,
    const DB::ASTPtr& queryAst,
    const DB::Context& context,
    const DB::ThrottlerPtr& throttler,
    const DB::Tables& externalTables,
    DB::QueryProcessingStage::Enum processedStage)
{
    const auto* queryContext = GetQueryContext(context);

    std::string query = queryToString(queryAst);

    // TODO(max42): can be done only once?
    DB::Block header = materializeBlock(DB::InterpreterSelectQuery(
        queryAst,
        context,
        DB::SelectQueryOptions(processedStage).analyze()).getSampleBlock());

    auto stream = std::make_shared<DB::RemoteBlockInputStream>(
        remoteNode->GetConnection(),
        query,
        header,
        context,
        nullptr,    // will use settings from context
        throttler,
        externalTables,
        processedStage);

    stream->setPoolMode(DB::PoolMode::GET_MANY);
    auto remoteQueryId = ToString(TQueryId::Create());
    stream->setRemoteQueryId(remoteQueryId);

    return CreateBlockInputStreamLoggingAdapter(std::move(stream), TLogger(queryContext->Logger)
        .AddTag("RemoteQueryId: %v, RemoteNode: %v, RemoteStreamId: %v",
            remoteQueryId,
            remoteNode->GetName().ToString(),
            TGuid::Create()));
}

DB::ASTPtr RewriteForSubquery(const DB::ASTPtr& queryAst, const std::string& subquerySpec, const TLogger& logger)
{
    const auto& Logger = logger;

    auto modifiedQueryAst = queryAst->clone();

    auto* tableExpression = GetFirstTableExpression(typeid_cast<DB::ASTSelectQuery &>(*modifiedQueryAst));
    YT_VERIFY(tableExpression);
    YT_VERIFY(!tableExpression->subquery);

    auto tableFunction = makeASTFunction(
        "ytSubquery",
        std::make_shared<DB::ASTLiteral>(subquerySpec));

    if (tableExpression->database_and_table_name) {
        tableFunction->alias = static_cast<DB::ASTWithAlias&>(*tableExpression->database_and_table_name).alias;
    } else {
        tableFunction->alias = static_cast<DB::ASTWithAlias&>(*tableExpression->table_function).alias;
    }

    auto oldTableExpression = tableExpression->clone();

    tableExpression->table_function = std::move(tableFunction);
    tableExpression->database_and_table_name = nullptr;
    tableExpression->subquery = nullptr;
    tableExpression->sample_offset = nullptr;
    tableExpression->sample_size = nullptr;

    YT_LOG_DEBUG("Rewriting for subquery (OldTableExpression: %v, NewTableExpression: %v)",
        queryToString(oldTableExpression),
        queryToString(tableExpression->clone()));

    return modifiedQueryAst;
}

/////////////////////////////////////////////////////////////////////////////

class TStorageDistributor
    : public DB::IStorage
{
public:
    TStorageDistributor(
        NTableClient::TTableSchema schema,
        TClickHouseTableSchema clickHouseSchema,
        std::vector<TRichYPath> tablePaths)
        : ClickHouseSchema_(std::move(clickHouseSchema))
        , Schema_(std::move(schema))
        , TablePaths_(std::move(tablePaths))
    { }

    virtual void startup() override
    {
        if (ClickHouseSchema_.Columns.empty()) {
            THROW_ERROR_EXCEPTION("CHYT does not support tables without schema")
                << TErrorAttribute("path", getTableName());
        }
        setColumns(DB::ColumnsDescription(ClickHouseSchema_.Columns));
        SpecTemplate_.Columns = ClickHouseSchema_.Columns;
        SpecTemplate_.ReadSchema = Schema_;
    }

    std::string getName() const override
    {
        return "StorageDistributor";
    }

    bool isRemote() const override
    {
        return true;
    }

    virtual bool supportsIndexForIn() const override
    {
        return ClickHouseSchema_.HasPrimaryKey();
    }

    virtual bool mayBenefitFromIndexForIn(const DB::ASTPtr& /* queryAst */, const DB::Context& /* context */) const override
    {
        return supportsIndexForIn();
    }

    virtual std::string getTableName() const
    {
        std::string result = "";
        for (size_t index = 0; index < TablePaths_.size(); ++index) {
            if (index > 0) {
                result += ", ";
            }
            result += std::string(TablePaths_[index].GetPath().data());
        }
        return result;
    }

    virtual DB::QueryProcessingStage::Enum getQueryProcessingStage(const DB::Context& /* context */) const override
    {
        return DB::QueryProcessingStage::WithMergeableState;
    }

    virtual DB::BlockInputStreams read(
        const DB::Names& columnNames,
        const DB::SelectQueryInfo& queryInfo,
        const DB::Context& context,
        DB::QueryProcessingStage::Enum processedStage,
        size_t /* maxBlockSize */,
        unsigned /* numStreams */) override
    {
        auto* queryContext = GetQueryContext(context);
        const auto& Logger = queryContext->Logger;

        SpecTemplate_.InitialQueryId = queryContext->QueryId;

        auto cliqueNodes = queryContext->Bootstrap->GetHost()->GetNodes();
        Prepare(cliqueNodes.size(), queryInfo, context);

        YT_LOG_INFO("Starting distribution (ColumnNames: %v, TableName: %v, NodeCount: %v, MaxThreads: %v, StripeCount: %v)",
            columnNames,
            getTableName(),
            cliqueNodes.size(),
            static_cast<ui64>(context.getSettings().max_threads),
            StripeList_->Stripes.size());

        if (cliqueNodes.empty()) {
            THROW_ERROR_EXCEPTION("There are no instances available through discovery");
        }

        const auto& settings = context.getSettingsRef();

        // TODO(max42): wtf?
        processedStage = settings.distributed_group_by_no_merge
            ? DB::QueryProcessingStage::Complete
            : DB::QueryProcessingStage::WithMergeableState;

        DB::Context newContext(context);
        newContext.setSettings(PrepareLeafJobSettings(settings));

        // TODO(max42): do we need them?
        auto throttler = CreateNetThrottler(settings);

        DB::BlockInputStreams streams;

        // TODO(max42): CHYT-154.
        SpecTemplate_.MembershipHint = DumpMembershipHint(*queryInfo.query, Logger);

        for (int index = 0; index < static_cast<int>(cliqueNodes.size()); ++index) {
            int firstStripeIndex = index * StripeList_->Stripes.size() / cliqueNodes.size();
            int lastStripeIndex = (index + 1) * StripeList_->Stripes.size() / cliqueNodes.size();

            const auto& cliqueNode = cliqueNodes[index];
            auto spec = SpecTemplate_;
            FillDataSliceDescriptors(
                spec,
                MakeRange(
                    StripeList_->Stripes.begin() + firstStripeIndex,
                    StripeList_->Stripes.begin() + lastStripeIndex));

            auto protoSpec = NYT::ToProto<NProto::TSubquerySpec>(spec);
            auto encodedSpec = Base64Encode(protoSpec.SerializeAsString());

            YT_LOG_DEBUG("Rewriting query (OriginalQuery: %v)", *queryInfo.query);
            auto subqueryAst = RewriteForSubquery(queryInfo.query, encodedSpec, Logger);
            YT_LOG_DEBUG("Query rewritten (Subquery: %v)", *subqueryAst);

            YT_LOG_DEBUG("Prepared subquery to node (Node: %v, StripeCount: %v)",
                cliqueNode->GetName().ToString(),
                lastStripeIndex - firstStripeIndex);

            bool isLocal = cliqueNode->IsLocal();
            // XXX(max42): weird workaround.
            isLocal = false;
            auto substream = isLocal
                ? CreateLocalStream(
                    subqueryAst,
                    newContext,
                    processedStage)
                : CreateRemoteStream(
                    cliqueNode,
                    subqueryAst,
                    newContext,
                    throttler,
                    context.getExternalTables(),
                    processedStage);

            streams.push_back(std::move(substream));
        }

        YT_LOG_INFO("Finished distribution");

        return streams;
    }

    virtual bool supportsSampling() const override
    {
        return true;
    }

    virtual DB::BlockOutputStreamPtr write(const DB::ASTPtr& /* ptr */, const DB::Context& context) override
    {
        auto* queryContext = GetQueryContext(context);
        // Set append if it is not set.

        if (TablePaths_.size() != 1) {
            THROW_ERROR_EXCEPTION("Cannot write to many tables simultaneously")
                << TErrorAttribute("paths", TablePaths_);
        }

        auto path = TablePaths_.front();
        path.SetAppend(path.GetAppend(true /* defaultValue */));
        auto writer = WaitFor(CreateSchemalessTableWriter(
            queryContext->Bootstrap->GetConfig()->TableWriterConfig,
            New<TTableWriterOptions>(),
            path,
            New<TNameTable>(),
            queryContext->Client(),
            nullptr /* transaction */))
            .ValueOrThrow();
        return CreateBlockOutputStream(std::move(writer), queryContext->Logger);
    }

private:
    TClickHouseTableSchema ClickHouseSchema_;
    NTableClient::TTableSchema Schema_;
    TSubquerySpec SpecTemplate_;
    NChunkPools::TChunkStripeListPtr StripeList_;
    std::vector<TRichYPath> TablePaths_;

    void Prepare(
        int subqueryCount,
        const DB::SelectQueryInfo& queryInfo,
        const DB::Context& context)
    {
        auto* queryContext = GetQueryContext(context);

        std::unique_ptr<DB::KeyCondition> keyCondition;
        if (ClickHouseSchema_.HasPrimaryKey()) {
            keyCondition = std::make_unique<DB::KeyCondition>(CreateKeyCondition(context, queryInfo, ClickHouseSchema_));
        }

        auto dataSlices = FetchDataSlices(
            queryContext->Client(),
            queryContext->Bootstrap->GetSerializedWorkerInvoker(),
            TablePaths_,
            keyCondition.get(),
            queryContext->RowBuffer,
            queryContext->Bootstrap->GetConfig()->Engine->Subquery,
            SpecTemplate_);

        i64 totalRowCount = 0;
        for (const auto& dataSlice : dataSlices) {
            totalRowCount += dataSlice->GetRowCount();
        }

        std::optional<double> samplingRate;
        const auto& selectQuery = queryInfo.query->as<DB::ASTSelectQuery&>();
        if (auto selectSampleSize = selectQuery.sample_size()) {
            auto ratio = selectSampleSize->as<DB::ASTSampleRatio&>().ratio;
            auto rate = static_cast<double>(ratio.numerator) / ratio.denominator;
            if (rate > 1.0) {
                rate /= totalRowCount;
            }
            rate = std::max(0.0, std::min(1.0, rate));
            samplingRate = rate;
        }

        StripeList_ = BuildThreadStripes(dataSlices, subqueryCount * context.getSettings().max_threads, samplingRate, queryContext->QueryId);
    }
};

////////////////////////////////////////////////////////////////////////////////

DB::StoragePtr CreateDistributorFromCH(DB::StorageFactory::Arguments args)
{
    auto* queryContext = GetQueryContext(args.local_context);
    const auto& client = queryContext->Client();
    const auto& Logger = queryContext->Logger;

    TKeyColumns keyColumns;

    if (args.storage_def->order_by) {
        auto orderByAst = args.storage_def->order_by->ptr();
        orderByAst = DB::MergeTreeData::extractKeyExpressionList(orderByAst);
        for (const auto& child : orderByAst->children) {
            auto* identifier = dynamic_cast<DB::ASTIdentifier*>(child.get());
            if (!identifier) {
                THROW_ERROR_EXCEPTION("CHYT does not support compound expressions as parts of key")
                    << TErrorAttribute("expression", child->getColumnName());
            }
            keyColumns.emplace_back(identifier->getColumnName());
        }
    }

    auto path = TRichYPath::Parse(TString(args.table_name));
    YT_LOG_INFO("Creating table from CH engine (Path: %v, Columns: %v, KeyColumns: %v)",
        path,
        args.columns.toString(),
        keyColumns);

    auto attributes = ConvertToAttributes(queryContext->Bootstrap->GetConfig()->Engine->CreateTableDefaultAttributes);
    if (!args.engine_args.empty()) {
        if (static_cast<int>(args.engine_args.size()) > 1) {
            THROW_ERROR_EXCEPTION("YtTable accepts at most one argument");
        }
        const auto* ast = args.engine_args[0]->as<DB::ASTLiteral>();
        if (ast && ast->value.getType() == DB::Field::Types::String) {
            auto extraAttributes = ConvertToAttributes(TYsonString(TString(DB::safeGet<std::string>(ast->value))));
            attributes->MergeFrom(*extraAttributes);
        } else {
            THROW_ERROR_EXCEPTION("Extra attributes must be a string literal");
        }
    }

    // Underscore indicates that the columns should be ignored, and that schema should be taken from the attributes.
    if (args.columns.getNamesOfPhysical() != std::vector<std::string>{"_"}) {
        auto schema = ConvertToTableSchema(args.columns, keyColumns);
        YT_LOG_DEBUG("Inferred table schema from columns (Schema: %v)", schema);
        attributes->Set("schema", schema);
    } else if (attributes->Contains("schema")) {
        YT_LOG_DEBUG("Table schema is taken from attributes (Schema: %v)", attributes->FindYson("schema"));
    } else {
        THROW_ERROR_EXCEPTION(
            "Table schema should be specified either by column list (possibly with ORDER BY) or by "
            "YT schema in attributes (as the only storage argument in YSON under key `schema`, in this case "
            "column list should consist of the only column named `_`)");
    };

    YT_LOG_DEBUG("Creating table (Attributes: %v)", ConvertToYsonString(attributes->ToMap(), EYsonFormat::Text));
    NApi::TCreateNodeOptions options;
    options.Attributes = std::move(attributes);
    auto id = WaitFor(client->CreateNode(path.GetPath(), NObjectClient::EObjectType::Table, options))
        .ValueOrThrow();
    YT_LOG_DEBUG("Table created (ObjectId: %v)", id);

    auto table = FetchClickHouseTable(client, path, Logger);
    YT_VERIFY(table);

    return std::make_shared<TStorageDistributor>(
        table->TableSchema,
        TClickHouseTableSchema::From(*table),
        std::vector<TRichYPath>{table->Path});
}

std::pair<TTableSchema, TClickHouseTableSchema> GetCommonSchema(const std::vector<TClickHouseTablePtr>& tables)
{
    // TODO(max42): code below looks like a good programming contest code, but seems strange as a production code.
    // Maybe rewrite it simpler?

    THashMap<TString, TClickHouseColumn> nameToColumn;
    THashMap<TString, int> nameToOccurrenceCount;
    for (const auto& tableColumn : tables[0]->Columns) {
        auto column = tableColumn;
        nameToColumn[column.Name] = column;
    }

    for (const auto& table : tables) {
        for (const auto& tableColumn : table->Columns) {
            auto column = tableColumn;

            bool columnTaken = false;
            auto it = nameToColumn.find(column.Name);
            if (it != nameToColumn.end()) {
                if (it->second == column) {
                    columnTaken = true;
                } else {
                    // There are at least two different variations of given column among provided tables,
                    // so we are not going to take it.
                }
            }

            if (columnTaken) {
                ++nameToOccurrenceCount[column.Name];
            }
        }
    }

    for (const auto& [name, occurrenceCount] : nameToOccurrenceCount) {
        if (occurrenceCount != static_cast<int>(tables.size())) {
            auto it = nameToColumn.find(name);
            YT_VERIFY(it != nameToColumn.end());
            nameToColumn.erase(it);
        }
    }

    if (nameToColumn.empty()) {
        THROW_ERROR_EXCEPTION("Requested tables do not have any common column");
    }

    std::vector<TClickHouseColumn> remainingColumns = tables[0]->Columns;
    remainingColumns.erase(std::remove_if(remainingColumns.begin(), remainingColumns.end(), [&] (const TClickHouseColumn& column) {
        return !nameToColumn.contains(column.Name);
    }), remainingColumns.end());

    // TODO(max42): extract as helper (there are two occurrences of this boilerplate code).
    const auto& dataTypes = DB::DataTypeFactory::instance();
    DB::NamesAndTypesList columns;
    DB::NamesAndTypesList keyColumns;
    DB::Names primarySortColumns;
    std::vector<TColumnSchema> columnSchemas;

    for (const auto& column : remainingColumns) {
        auto dataType = dataTypes.get(GetTypeName(column));
        if (column.IsNullable()) {
            dataType = DB::makeNullable(dataType);
        }
        columns.emplace_back(column.Name, dataType);
        auto& columnSchema = columnSchemas.emplace_back(tables[0]->TableSchema.GetColumn(column.Name));

        if (column.IsSorted()) {
            keyColumns.emplace_back(column.Name, dataType);
            primarySortColumns.emplace_back(column.Name);
        } else {
            columnSchema.SetSortOrder(std::nullopt);
        }
    }

    return {TTableSchema(std::move(columnSchemas)), TClickHouseTableSchema(std::move(columns), std::move(keyColumns), std::move(primarySortColumns))};
}

////////////////////////////////////////////////////////////////////////////////

DB::StoragePtr CreateStorageDistributor(std::vector<TClickHouseTablePtr> tables)
{
    if (tables.empty()) {
        THROW_ERROR_EXCEPTION("Cannot concatenate empty list of tables");
    }

    TTableSchema schema;
    TClickHouseTableSchema clickHouseSchema;
    if (tables.size() > 1) {
        std::tie(schema, clickHouseSchema) = GetCommonSchema(tables);
    } else {
        schema = tables.front()->TableSchema;
        clickHouseSchema = TClickHouseTableSchema::From(*tables.front());
    }

    std::vector<TRichYPath> paths;
    for (const auto& table : tables) {
        paths.emplace_back(table->Path);
    }

    auto storage = std::make_shared<TStorageDistributor>(
        std::move(schema),
        std::move(clickHouseSchema),
        std::move(paths));
    storage->startup();

    return storage;
}

void RegisterStorageDistributor()
{
    auto& factory = DB::StorageFactory::instance();
    // TODO(max42): do not create distributor; create some specific StorageWriter instead.
    factory.registerStorage("YtTable", CreateDistributorFromCH);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NClickHouseServer
