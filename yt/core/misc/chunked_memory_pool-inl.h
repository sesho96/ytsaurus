#pragma once
#ifndef CHUNKED_MEMORY_POOL_INL_H_
#error "Direct inclusion of this file is not allowed, include chunked_memory_pool.h"
// For the sake of sane code completion.
#include "chunked_memory_pool.h"
#endif

#include "serialize.h"

#include <library/cpp/ytalloc/api/ytalloc.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

inline void TAllocationHolder::operator delete(void* ptr) noexcept
{
    NYTAlloc::FreeNonNull(ptr);
}

inline TMutableRef TAllocationHolder::GetRef() const
{
    return Ref_;
}

template <class TDerived>
TDerived* TAllocationHolder::Allocate(size_t size, TRefCountedTypeCookie cookie)
{
    auto requestedSize = sizeof(TDerived) + size;
    auto* ptr = NYTAlloc::Allocate(requestedSize);
    auto allocatedSize = NYTAlloc::GetAllocationSize(ptr);
    if (allocatedSize) {
        size += allocatedSize - requestedSize;
    }

    auto* instance = static_cast<TDerived*>(ptr);

    try {
        new (instance) TDerived(TMutableRef(instance + 1, size), cookie);
    } catch (const std::exception& ex) {
        // Do not forget to free the memory.
        NYTAlloc::FreeNonNull(ptr);
        throw;
    }

    return instance;
}

////////////////////////////////////////////////////////////////////////////////

inline TChunkedMemoryPool::TChunkedMemoryPool()
    : TChunkedMemoryPool(
        GetRefCountedTypeCookie<TDefaultChunkedMemoryPoolTag>())
{ }

template <class TTag>
inline TChunkedMemoryPool::TChunkedMemoryPool(
    TTag,
    size_t startChunkSize)
    : TChunkedMemoryPool(
        GetRefCountedTypeCookie<TTag>(),
        startChunkSize)
{ }

inline char* TChunkedMemoryPool::AllocateUnaligned(size_t size)
{
    // Fast path.
    if (FreeZoneEnd_ >= FreeZoneBegin_ + size) {
        FreeZoneEnd_ -= size;
        Size_ += size;
        return FreeZoneEnd_;
    }

    // Slow path.
    return AllocateUnalignedSlow(size);
}

inline char* TChunkedMemoryPool::AllocateAligned(size_t size, int align)
{
    // NB: This can lead to FreeZoneBegin_ >= FreeZoneEnd_ in which case the chunk is full.
    FreeZoneBegin_ = AlignUp(FreeZoneBegin_, align);

    // Fast path.
    if (FreeZoneBegin_ + size <= FreeZoneEnd_) {
        char* result = FreeZoneBegin_;
        Size_ += size;
        FreeZoneBegin_ += size;
        return result;
    }

    // Slow path.
    return AllocateAlignedSlow(size, align);
}

template <class T>
inline T* TChunkedMemoryPool::AllocateUninitialized(int n, int align)
{
    return reinterpret_cast<T*>(AllocateAligned(sizeof(T) * n, align));
}

inline char* TChunkedMemoryPool::Capture(TRef src, int align)
{
    auto* dst = AllocateAligned(src.Size(), align);
    ::memcpy(dst, src.Begin(), src.Size());
    return dst;
}

inline void TChunkedMemoryPool::Free(char* from, char* to)
{
    if (FreeZoneBegin_ == to) {
        FreeZoneBegin_ = from;
    }
    if (FreeZoneEnd_ == from) {
        FreeZoneEnd_ = to;
    }
}

inline void TChunkedMemoryPool::Clear()
{
    Size_ = 0;

    if (Chunks_.empty()) {
        FreeZoneBegin_ = nullptr;
        FreeZoneEnd_ = nullptr;
        NextChunkIndex_ = 0;
    } else {
        FreeZoneBegin_ = Chunks_.front()->GetRef().Begin();
        FreeZoneEnd_ = Chunks_.front()->GetRef().End();
        NextChunkIndex_ = 1;
    }

    for (const auto& block : OtherBlocks_) {
        Capacity_ -= block->GetRef().Size();
    }
    OtherBlocks_.clear();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
