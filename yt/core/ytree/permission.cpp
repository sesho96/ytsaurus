#include "permission.h"

#include <yt/core/misc/error.h>
#include <yt/core/misc/string.h>

namespace NYT::NYTree {

////////////////////////////////////////////////////////////////////////////////

EPermissionSet ParsePermissions(const std::vector<TString>& items)
{
    auto result = NonePermissions;
    for (const auto& item : items) {
        result |= ParseEnum<EPermission>(item);
    }
    return result;
}

std::vector<TString> FormatPermissions(EPermissionSet permissions)
{
    std::vector<TString> result;
    for (auto value : TEnumTraits<EPermission>::GetDomainValues()) {
        if ((permissions & value) != NonePermissions) {
            result.push_back(FormatEnum(value));
        }
    }
    return result;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NYTree

