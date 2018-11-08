#include "serialize.h"

namespace NYT {
namespace NControllerAgent {

////////////////////////////////////////////////////////////////////////////////

int GetCurrentSnapshotVersion()
{
    return 300023;
}

bool ValidateSnapshotVersion(int version)
{
    return version >= 300023 && version <= GetCurrentSnapshotVersion();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NControllerAgent
} // namespace NYT
