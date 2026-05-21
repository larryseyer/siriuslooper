#pragma once

#include "sirius/TapeId.h"

#include <string>

namespace sirius
{

/// One entry in the project tape pool: light metadata naming a tape that exists
/// as a capture destination. Parallel to InputDescriptor — honors the white
/// paper section 7.2 data-layer / structure-layer split: the heavy Tape<Payload>
/// stream does not know about descriptors; this points at a tape by id and
/// gives it a name.
struct TapeDescriptor
{
    TapeId      id;
    std::string name;

    bool operator== (const TapeDescriptor& other) const noexcept
    {
        return id == other.id && name == other.name;
    }
};

} // namespace sirius
