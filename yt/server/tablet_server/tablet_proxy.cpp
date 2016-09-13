#include "tablet_proxy.h"
#include "private.h"
#include "tablet.h"
#include "tablet_cell.h"
#include "tablet_manager.h"

#include <yt/server/cell_master/bootstrap.h>

#include <yt/server/chunk_server/chunk_list.h>

#include <yt/server/object_server/object_detail.h>

#include <yt/server/table_server/table_node.h>

#include <yt/core/yson/consumer.h>

#include <yt/core/ytree/fluent.h>

namespace NYT {
namespace NTabletServer {

using namespace NYson;
using namespace NYTree;
using namespace NObjectServer;

////////////////////////////////////////////////////////////////////////////////

class TTabletProxy
    : public TNonversionedObjectProxyBase<TTablet>
{
public:
    TTabletProxy(
        NCellMaster::TBootstrap* bootstrap,
        TObjectTypeMetadata* metadata,
        TTablet* tablet)
        : TBase(bootstrap, metadata, tablet)
    { }

private:
    typedef TNonversionedObjectProxyBase<TTablet> TBase;

    virtual void ListSystemAttributes(std::vector<TAttributeDescriptor>* descriptors) override
    {
        TBase::ListSystemAttributes(descriptors);

        const auto* tablet = GetThisImpl();
        const auto* table = tablet->GetTable();

        descriptors->push_back("state");
        descriptors->push_back("statistics");
        descriptors->push_back(TAttributeDescriptor("trimmed_row_count")
            .SetPresent(!table->IsPhysicallySorted()));
        descriptors->push_back(TAttributeDescriptor("flushed_row_count")
            .SetPresent(!table->IsPhysicallySorted()));
        descriptors->push_back(TAttributeDescriptor("performance_counters")
            .SetPresent(tablet->GetCell()));
        descriptors->push_back(TAttributeDescriptor("mount_revision")
            .SetPresent(tablet->GetCell()));
        descriptors->push_back("index");
        descriptors->push_back("table_id");
        descriptors->push_back(TAttributeDescriptor("pivot_key")
            .SetPresent(table->IsPhysicallySorted()));
        descriptors->push_back("chunk_list_id");
        descriptors->push_back("in_memory_mode");
        descriptors->push_back(TAttributeDescriptor("cell_id")
            .SetPresent(tablet->GetCell()));
    }

    virtual bool GetBuiltinAttribute(const Stroka& key, IYsonConsumer* consumer) override
    {
        const auto* tablet = GetThisImpl();
        const auto* table = tablet->GetTable();
        const auto* chunkList = tablet->GetChunkList();

        auto tabletManager = Bootstrap_->GetTabletManager();

        if (key == "state") {
            BuildYsonFluently(consumer)
                .Value(tablet->GetState());
            return true;
        }

        if (key == "statistics") {
            BuildYsonFluently(consumer)
                .Value(tabletManager->GetTabletStatistics(tablet));
            return true;
        }

        if (key == "trimmed_row_count") {
            BuildYsonFluently(consumer)
                .Value(tablet->GetTrimmedRowCount());
            return true;
        }

        if (key == "flushed_row_count") {
            BuildYsonFluently(consumer)
                .Value(chunkList->Statistics().LogicalRowCount);
            return true;
        }

        if (tablet->GetCell()) {
            if (key == "performance_counters") {
                BuildYsonFluently(consumer)
                    .Value(tablet->PerformanceCounters());
                return true;
            }

            if (key == "mount_revision") {
                BuildYsonFluently(consumer)
                    .Value(tablet->GetMountRevision());
                return true;
            }
        }

        if (key == "index") {
            BuildYsonFluently(consumer)
                .Value(tablet->GetIndex());
            return true;
        }

        if (key == "table_id") {
            BuildYsonFluently(consumer)
                .Value(table->GetId());
            return true;
        }

        if (key == "pivot_key" && table->IsPhysicallySorted()) {
            BuildYsonFluently(consumer)
                .Value(tablet->GetPivotKey());
            return true;
        }

        if (key == "chunk_list_id") {
            BuildYsonFluently(consumer)
                .Value(tablet->GetChunkList()->GetId());
            return true;
        }

        if (key == "in_memory_mode") {
            BuildYsonFluently(consumer)
                .Value(tablet->GetInMemoryMode());
            return true;
        }

        if (tablet->GetCell()) {
            if (key == "cell_id") {
                BuildYsonFluently(consumer)
                    .Value(tablet->GetCell()->GetId());
                return true;
            }
        }

        return TBase::GetBuiltinAttribute(key, consumer);
    }
};

IObjectProxyPtr CreateTabletProxy(
    NCellMaster::TBootstrap* bootstrap,
    TObjectTypeMetadata* metadata,
    TTablet* tablet)
{
    return New<TTabletProxy>(bootstrap, metadata, tablet);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletServer
} // namespace NYT

