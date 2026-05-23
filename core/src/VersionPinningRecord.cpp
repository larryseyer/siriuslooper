#include "ida/VersionPinningRecord.h"

#include "ida/Sha256.h"

namespace ida
{

bool VersionPinningRecord::matches (const VersionPinningRecord& other) const noexcept
{
    return uniqueId == other.uniqueId
        && version == other.version
        && stateBlobSha256 == other.stateBlobSha256
        && declaredInternalStateHash == other.declaredInternalStateHash;
}

bool VersionPinningRecord::operator== (const VersionPinningRecord& other) const noexcept
{
    return uniqueId == other.uniqueId
        && version == other.version
        && stateBlobSha256 == other.stateBlobSha256
        && oversamplingRate == other.oversamplingRate
        && declaredInternalStateHash == other.declaredInternalStateHash;
}

VersionPinningRecord makeVersionPinningRecord (const PluginDescriptor& descriptor,
                                               std::span<const std::byte> stateBlob)
{
    VersionPinningRecord r;
    r.uniqueId        = descriptor.uniqueId;
    r.version         = descriptor.version;
    r.stateBlobSha256 = sha256Hex (stateBlob);
    return r;
}

} // namespace ida
