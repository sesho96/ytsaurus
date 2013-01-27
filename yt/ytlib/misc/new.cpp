#include "stdafx.h"
#include "common.h"
#include "new.h"
#include "ref_counted_tracker.h"

namespace NYT {
namespace NDetail {

////////////////////////////////////////////////////////////////////////////////

void* GetRefCountedTrackerCookie(const void* key)
{
    return TRefCountedTracker::Get()->GetCookie(
        static_cast<const std::type_info*>(key));
}

////////////////////////////////////////////////////////////////////////////////
    
} // namespace NDetail
} // namespace NYT
