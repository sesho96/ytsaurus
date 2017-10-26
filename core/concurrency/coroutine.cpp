#include "coroutine.h"

namespace NYT {
namespace NConcurrency {
namespace NDetail {

////////////////////////////////////////////////////////////////////////////////

TCoroutineBase::TCoroutineBase()
    : CoroutineStack_(CreateExecutionStack(EExecutionStackKind::Small))
    , CoroutineContext_(CoroutineStack_.get(), this)
{ }

void TCoroutineBase::DoRun()
{
    try {
        Invoke();
    } catch (...) {
        CoroutineException_ = std::current_exception();
    }

    Completed_ = true;
    JumpToCaller();

    Y_UNREACHABLE();
}

void TCoroutineBase::JumpToCaller()
{
    CoroutineContext_.SwitchTo(&CallerContext_);
}

void TCoroutineBase::JumpToCoroutine()
{
    CallerContext_.SwitchTo(&CoroutineContext_);

    if (CoroutineException_) {
        std::exception_ptr exception;
        std::swap(exception, CoroutineException_);
        std::rethrow_exception(std::move(exception));
    }
}

bool TCoroutineBase::IsCompleted() const
{
    return Completed_;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NDetail
} // namespace NConcurrency
} // namespace NYT
