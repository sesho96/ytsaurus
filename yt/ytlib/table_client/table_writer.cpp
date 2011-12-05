﻿#include "stdafx.h"
#include "table_writer.h"

#include "../misc/sync.h"
#include "../cypress/cypress_ypath_rpc.h"

namespace NYT {
namespace NTableClient {

using namespace NTransactionClient;
using namespace NCypress;
using namespace NTableServer;

////////////////////////////////////////////////////////////////////////////////

TTableWriter::TTableWriter(
    const TConfig& config,
    NRpc::IChannel::TPtr masterChannel,
    ITransaction::TPtr transaction,
    const TSchema& schema,
    const Stroka& path)
    : Config(config)
    , Path(path)
    , Transaction(transaction)
    , MasterChannel(masterChannel)
    , Writer(New<TChunkSequenceWriter>(
        config.ChunkSetConfig, 
        schema, 
        Transaction->GetId(), 
        MasterChannel))
    , Proxy(~masterChannel)
{
    YASSERT(~masterChannel != NULL);
    YASSERT(~transaction != NULL);

    Proxy.SetTimeout(Config.RpcTimeout);

    OnAborted_ = FromMethod(
        &TTableWriter::OnAborted,
        TPtr(this));
    Transaction->SubscribeAborted(OnAborted_);

    if (!NodeExists(path)) {
        CreateTableNode(path);
    }
}

void TTableWriter::Open()
{
    Sync(~Writer, &TChunkSequenceWriter::AsyncOpen);
}

bool TTableWriter::NodeExists(const Stroka& path)
{
    auto req = TCypressYPathProxy::GetId();

    auto rsp = Proxy.Execute(path, Transaction->GetId(), ~req)->Get();

    if (!rsp->IsOK()) {
        const auto& error = rsp->GetError();
        if (NRpc::IsRpcError(error)) {
            Writer->Cancel(error.ToString());
            ythrow yexception() << Sprintf("Error 2checking table for existence (Path: %s)\n%s",
                ~path,
                ~error.ToString());
        }
        return false;
    }

    NodeId = TNodeId::FromProto(rsp->GetNodeId());
    return true;
}

void TTableWriter::CreateTableNode(const Stroka& nodePath)
{
    auto req = TCypressYPathProxy::Create();
    req->SetType("table");
    req->SetManifest("{}");

    auto rsp = Proxy.Execute(nodePath, Transaction->GetId(), ~req)->Get();

    if (!rsp->IsOK()) {
        const auto& error = rsp->GetError();
        Writer->Cancel(error.ToString());
        ythrow yexception() << Sprintf("Error creating table (Path: %s)\n%s",
            ~nodePath,
            ~error.ToString());
    }

    NodeId = TNodeId::FromProto(rsp->GetNodeId());
}

void TTableWriter::Write(const TColumn& column, TValue value)
{
    Writer->Write(column, value);
}

void TTableWriter::EndRow()
{
    Sync(~Writer, &TChunkSequenceWriter::AsyncEndRow);
}

void TTableWriter::Close()
{
    Sync(~Writer, &TChunkSequenceWriter::AsyncClose);

    auto req = TTableYPathProxy::AddTableChunks();
    ToProto<TChunkId, Stroka>(*req->MutableChunkIds(), Writer->GetWrittenChunks());

    auto rsp = Proxy.Execute(
        GetYPathFromNodeId(NodeId),
        Transaction->GetId(),
        ~req)->Get();

    if (!rsp->IsOK()) {
        const auto& error = rsp->GetError();
        ythrow yexception() << Sprintf("Error adding chunks to table (NodeId: %s)\n%s",
            ~NodeId.ToString(),
            ~error.ToString());
    }

    Finish();
}

void TTableWriter::OnAborted()
{
    Writer->Cancel("Transaction aborted");
    Finish();
}

void TTableWriter::Finish()
{
    Transaction->UnsubscribeAborted(OnAborted_);
    OnAborted_.Reset();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTableClient
} // namespace NYT
