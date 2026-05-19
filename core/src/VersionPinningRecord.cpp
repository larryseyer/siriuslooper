#include "sirius/VersionPinningRecord.h"

#include "sirius/Sha256.h"

namespace sirius
{

bool VersionPinningRecord::matches (const VersionPinningRecord& other) const noexcept
{
    return uniqueId == other.uniqueId
        && version == other.version
        && stateBlobSha256 == other.stateBlobSha256
        && declaredInternalStateHash == other.declaredInternalStateHash;
}

VersionPinningRecord makeVersionPinningRecord (const PluginDescriptor& descriptor,
                                               std::span<const std::byte> stateBlob)
{
    VersionPinningRecord r;
    r.uniqueId         = descriptor.uniqueId;
    r.version          = descriptor.version;
    r.stateBlobSha256  = sha256Hex (stateBlob);
    r.oversamplingRate = 1u;
    return r;
}

} // namespace sirius
