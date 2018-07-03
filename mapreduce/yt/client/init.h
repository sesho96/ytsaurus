#pragma once

#include <mapreduce/yt/interface/init.h>

namespace NYT {
namespace NDetail {

////////////////////////////////////////////////////////////////////////////////

enum class EInitStatus : int
{
    NotInitialized,
    JoblessInitialization,
    FullInitialization,
};

EInitStatus& GetInitStatus();

////////////////////////////////////////////////////////////////////////////////

} // namespace NDetail
} // namespace NYT
