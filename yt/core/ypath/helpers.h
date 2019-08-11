#pragma once

#include "public.h"

namespace NYT::NYPath {

////////////////////////////////////////////////////////////////////////////////

std::optional<TYPath> TryComputeYPathSuffix(const TYPath& path, const TYPath& prefix);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NYPath
