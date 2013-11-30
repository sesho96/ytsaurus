#include "stdafx.h"

#include <yt/core/misc/nullable.h>

#include <yt/core/yson/public.h>

#include <yt/core/ytree/convert.h>
#include <yt/core/ytree/node.h>

#include <yt/ytlib/tablet_client/public.h>

#include <yt/ytlib/new_table_client/public.h>
#include <yt/ytlib/new_table_client/schema.h>
#include <yt/ytlib/new_table_client/row.h>
#include <yt/ytlib/new_table_client/name_table.h>

#include <yt/server/tablet_node/static_memory_store.h>
#include <yt/server/tablet_node/dynamic_memory_store.h>
#include <yt/server/tablet_node/tablet.h>

#include <contrib/testing/framework.h>

namespace NYT {
namespace NTabletNode {
namespace {

using namespace NTabletClient;
using namespace NVersionedTableClient;
using namespace NYson;
using namespace NYTree;

////////////////////////////////////////////////////////////////////////////////

class TMemoryStoreTestBase
    : public ::testing::Test
{
public:
    TMemoryStoreTestBase()
    {
        NameTable = New<TNameTable>();

        TKeyColumns keyColumns;
        keyColumns.push_back("key");

        TTableSchema schema;
        // NB: Key columns must go first.
        schema.Columns().push_back(TColumnSchema("key", EValueType::Integer));
        schema.Columns().push_back(TColumnSchema("a", EValueType::Integer));
        schema.Columns().push_back(TColumnSchema("b", EValueType::Double));
        schema.Columns().push_back(TColumnSchema("c", EValueType::String));

        for (const auto& column : schema.Columns()) {
            NameTable->RegisterName(column.Name);
        }

        Tablet.reset(new TTablet(
            NullTabletId,
            schema,
            keyColumns,
            New<TTableMountConfig>()));
    }


    static TUnversionedOwningRow BuildKey(const Stroka& yson)
    {
        TUnversionedRowBuilder keyBuilder;
        auto keyParts = ConvertTo<std::vector<INodePtr>>(TYsonString(yson, EYsonType::ListFragment));
        for (auto keyPart : keyParts) {
            switch (keyPart->GetType()) {
                case ENodeType::Integer:
                    keyBuilder.AddValue(MakeIntegerValue<TUnversionedValue>(keyPart->GetValue<i64>()));
                    break;
                case ENodeType::Double:
                    keyBuilder.AddValue(MakeDoubleValue<TUnversionedValue>(keyPart->GetValue<double>()));
                    break;
                case ENodeType::String:
                    keyBuilder.AddValue(MakeStringValue<TUnversionedValue>(keyPart->GetValue<Stroka>()));
                    break;
                default:
                    keyBuilder.AddValue(MakeAnyValue<TUnversionedValue>(ConvertToYsonString(keyPart).Data()));
                    break;
            }
        }
        return keyBuilder.GetRow();
    }

    TVersionedOwningRow BuildRow(const Stroka& yson)
    {
        auto rowParts = ConvertTo<yhash_map<Stroka, INodePtr>>(TYsonString(yson, EYsonType::MapFragment));

        TVersionedRowBuilder rowBuilder;
        auto addValue = [&] (int id, INodePtr value) {
            switch (value->GetType()) {
                case ENodeType::Integer:
                    rowBuilder.AddValue(MakeIntegerValue<TVersionedValue>(value->GetValue<i64>(), id));
                    break;
                case ENodeType::Double:
                    rowBuilder.AddValue(MakeDoubleValue<TVersionedValue>(value->GetValue<double>(), id));
                    break;
                case ENodeType::String:
                    rowBuilder.AddValue(MakeStringValue<TVersionedValue>(value->GetValue<Stroka>(), id));
                    break;
                default:
                    rowBuilder.AddValue(MakeAnyValue<TVersionedValue>(ConvertToYsonString(value).Data(), id));
                    break;
            }
        };

        // Key
        for (int id = 0; id < static_cast<int>(Tablet->KeyColumns().size()); ++id) {
            auto it = rowParts.find(NameTable->GetName(id));
            YCHECK(it != rowParts.end());
            addValue(id, it->second);
        }

        // Fixed values
        for (int id = static_cast<int>(Tablet->KeyColumns().size()); id < static_cast<int>(Tablet->Schema().Columns().size()); ++id) {
            auto it = rowParts.find(NameTable->GetName(id));
            if (it != rowParts.end()) {
                addValue(id, it->second);
            } else {
                rowBuilder.AddValue(MakeSentinelValue<TVersionedValue>(EValueType::Null, id));
            }
        }

        // Variable values
        for (const auto& pair : rowParts) {
            int id = NameTable->GetIdOrRegisterName(pair.first);
            if (id >= Tablet->Schema().Columns().size()) {
                addValue(id, pair.second);
            }
        }

        return rowBuilder.GetRow();
    }

    void CheckRow(TVersionedRow row, const TNullable<Stroka>& yson)
    {
        if (!row && !yson)
            return;

        ASSERT_TRUE(static_cast<bool>(row));
        ASSERT_TRUE(yson.HasValue());

        auto expectedRowParts = ConvertTo<yhash_map<Stroka, INodePtr>>(TYsonString(*yson, EYsonType::MapFragment));

        for (int index = 0; index < row.GetValueCount(); ++index) {
            const auto& value = row[index];
            const auto& name = NameTable->GetName(value.Id);
            auto it = expectedRowParts.find(name);
            switch (value.Type) {
                case EValueType::Integer:
                    ASSERT_EQ(it->second->GetValue<i64>(), value.Data.Integer);
                    break;
                
                case EValueType::Double:
                    ASSERT_EQ(it->second->GetValue<double>(), value.Data.Double);
                    break;
                
                case EValueType::String:
                    ASSERT_EQ(it->second->GetValue<Stroka>(), Stroka(value.Data.String, value.Length));
                    break;

                case EValueType::Null:
                    ASSERT_TRUE(it == expectedRowParts.end());
                    break;

                default:
                    YUNREACHABLE();
            }
        }
    }


    TNameTablePtr NameTable;
    std::unique_ptr<TTablet> Tablet;

};

///////////////////////////////////////////////////////////////////////////////

} // namespace
} // namespace NTabletNode
} // namespace NYT
