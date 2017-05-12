#include "tablet_cell_bundle.h"
#include "tablet_cell.h"

#include <yt/ytlib/tablet_client/config.h>

namespace NYT {
namespace NTabletServer {

using namespace NCellMaster;
using namespace NObjectServer;
using namespace NTabletClient;

////////////////////////////////////////////////////////////////////////////////

TTabletCellBundle::TTabletCellBundle(const TTabletCellBundleId& id)
    : TNonversionedObjectBase(id)
    , Acd_(this)
    , Options_(New<TTabletCellOptions>())
{ }

void TTabletCellBundle::Save(TSaveContext& context) const
{
    TNonversionedObjectBase::Save(context);

    using NYT::Save;
    Save(context, Name_);
    Save(context, Acd_);
    Save(context, *Options_);
    Save(context, NodeTagFilter_);
    Save(context, TabletCells_);
}

void TTabletCellBundle::Load(TLoadContext& context)
{
    TNonversionedObjectBase::Load(context);

    using NYT::Load;
    Load(context, Name_);
    // COMPAT(babenko)
    if (context.GetVersion() >= 400) {
        Load(context, Acd_);
    }
    Load(context, *Options_);
    // COMPAT(babenko)
    if (context.GetVersion() >= 400) {
        // COMPAT(savrus)
        if (context.GetVersion() >= 600) {
            Load(context, NodeTagFilter_);
        } else {
            if (auto filter = Load<TNullable<TString>>(context)) {
                NodeTagFilter_ = MakeBooleanFormula(*filter);
            }
        }
    }
    // COMAPT(babenko)
    if (context.GetVersion() >= 400) {
        Load(context, TabletCells_);
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletServer
} // namespace NYT

