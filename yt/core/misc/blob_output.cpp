#include "blob_output.h"

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

static constexpr size_t InitialBlobOutputCapacity = 16;
static constexpr double BlobOutputCapacityMultiplier = 1.5;

struct TBlobOutputTag { };

TBlobOutput::TBlobOutput()
    : Blob_(TBlobOutputTag())
{ }

TBlobOutput::TBlobOutput(size_t capacity, size_t alignment)
    : Blob_(TBlobOutputTag(), 0, true, alignment)
{
    Reserve(capacity);
}

TBlobOutput::~TBlobOutput()
{ }

size_t TBlobOutput::DoNext(void** ptr)
{
    if (Blob_.Size() == Blob_.Capacity()) {
        if (Blob_.Capacity() >= InitialBlobOutputCapacity) {
            Reserve(static_cast<size_t>(Blob_.Capacity() * BlobOutputCapacityMultiplier));
        } else {
            Reserve(InitialBlobOutputCapacity);
        }
    }
    *ptr = Blob_.Begin() + Blob_.Size();
    return Blob_.Capacity() - Blob_.Size();
}

void TBlobOutput::DoAdvance(size_t len)
{
    YT_ASSERT(Blob_.Size() + len <= Blob_.Capacity());
    Blob_.Resize(Blob_.Size() + len, false);
}

void TBlobOutput::DoWrite(const void* buffer, size_t length)
{
    Blob_.Append(buffer, length);
}

void TBlobOutput::Reserve(size_t capacity)
{
    Blob_.Reserve(RoundUpToPage(capacity));
}

void TBlobOutput::Clear()
{
    Blob_.Clear();
}

TSharedRef TBlobOutput::Flush()
{
    auto result = TSharedRef::FromBlob(std::move(Blob_));
    Blob_.Clear();
    return result;
}

void swap(TBlobOutput& left, TBlobOutput& right)
{
    if (&left != &right) {
        swap(left.Blob_, right.Blob_);
    }
}

const TBlob& TBlobOutput::Blob() const
{
    return Blob_;
}

const char* TBlobOutput::Begin() const
{
    return Blob_.Begin();
}

size_t TBlobOutput::Size() const
{
    return Blob_.Size();
}

size_t TBlobOutput::Capacity() const
{
    return Blob_.Capacity();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
