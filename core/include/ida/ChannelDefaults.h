#pragma once

#include "ida/TapeMode.h"

namespace ida
{

/// Initial-value bundle for channels created from a given input. Carried on
/// `InputDescriptor::defaults` so callers can specify channel preferences at
/// registration time without forcing a follow-up `set_*` call per field.
/// `InputMixer` reads this struct on `registerInput` and uses it as the
/// starting point for any `addChannel` call against the input that does not
/// override the field explicitly.
///
/// Defaults are operator-friendly: `NoTape` (don't allocate storage unless
/// the operator asks for it) and `enabled = true` (a registered input is
/// usable immediately; the operator can disable it).
struct ChannelDefaults
{
    TapeMode defaultTapeMode { TapeMode::NoTape };
    bool defaultEnabled { true };
};

} // namespace ida
