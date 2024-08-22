#pragma once

#include "engine.h"

namespace NYT::NQueryTracker {

////////////////////////////////////////////////////////////////////////////////

IQueryEnginePtr CreateMockEngine(const NApi::IClientPtr& stateClient, const NYPath::TYPath& stateRoot);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NQueryTracker
