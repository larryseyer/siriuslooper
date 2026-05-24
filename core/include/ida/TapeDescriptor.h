#pragma once

#include "ida/TapeColorMode.h"
#include "ida/TapeId.h"

#include <string>

namespace ida
{

/// One entry in the project tape pool: light metadata naming a tape that exists
/// as a capture destination. Parallel to InputDescriptor — honors the white
/// paper section 7.2 data-layer / structure-layer split: the heavy Tape<Payload>
/// stream does not know about descriptors; this points at a tape by id and
/// gives it a name.
///
/// `tapeColor` is the per-tape TAPECOLOR mode (TAPECOLOR Slice 2, operator
/// design lock 2026-05-24). Default `None`. See `TapeColorMode` for semantics.
struct TapeDescriptor
{
    TapeId        id;
    std::string   name;
    TapeColorMode tapeColor { TapeColorMode::None };

    bool operator== (const TapeDescriptor& other) const noexcept
    {
        return id == other.id && name == other.name && tapeColor == other.tapeColor;
    }
};

} // namespace ida
