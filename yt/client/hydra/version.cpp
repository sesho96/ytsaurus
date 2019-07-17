#include "version.h"

#include <yt/core/misc/format.h>

namespace NYT::NHydra {

////////////////////////////////////////////////////////////////////////////////

TVersion::TVersion() noexcept
    : SegmentId(0)
    , RecordId(0)
{ }

TVersion::TVersion(int segmentId, int recordId) noexcept
    : SegmentId(segmentId)
    , RecordId(recordId)
{ }

bool TVersion::operator < (TVersion other) const
{
    return
        SegmentId < other.SegmentId ||
        (SegmentId == other.SegmentId && RecordId < other.RecordId);
}

bool TVersion::operator == (TVersion other) const
{
    return SegmentId == other.SegmentId && RecordId == other.RecordId;
}

bool TVersion::operator != (TVersion other) const
{
    return !(*this == other);
}

bool TVersion::operator > (TVersion other) const
{
    return !(*this <= other);
}

bool TVersion::operator <= (TVersion other) const
{
    return *this < other || *this == other;
}

bool TVersion::operator >= (TVersion other) const
{
    return !(*this < other);
}

ui64 TVersion::ToRevision() const
{
    return (static_cast<ui64>(SegmentId) << 32) | static_cast<ui64>(RecordId);
}

TVersion TVersion::FromRevision(ui64 revision)
{
    return TVersion(revision >> 32, revision & 0xffffffff);
}

TVersion TVersion::Advance(int delta) const
{
    YT_ASSERT(delta >= 0);
    return TVersion(SegmentId, RecordId + delta);
}

TVersion TVersion::Rotate() const
{
    return TVersion(SegmentId + 1, 0);
}

void FormatValue(TStringBuilderBase* builder, TVersion version, TStringBuf /*spec*/)
{
    builder->AppendFormat("%v:%v", version.SegmentId, version.RecordId);
}

TString ToString(TVersion version)
{
    return ToStringViaBuilder(version);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NHydra
