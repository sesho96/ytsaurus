#include "client.h"

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

void ILock::Wait(TDuration timeout)
{
    return GetAcquiredFuture().GetValue(timeout);
}

void ITransaction::Detach()
{
    Y_ABORT("ITransaction::Detach() is not implemented");
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
