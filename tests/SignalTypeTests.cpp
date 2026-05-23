// Tests for ida::SignalType and the signalTypeOf(InputKind) projection
// added in M2 Session 2. SignalType is the four-case modality the V3 mixer
// architecture uses (Audio / Midi / Video / File); InputKind is the seven-
// case descriptor enum InputDescriptor already carries. Per V7 alignment
// plan M2 "Risks & open decisions": Audio/Midi/Video pass through;
// Control, ParameterAutomation, Transport, and System all collapse to File
// until the M11 IAF format design forces a finer split.
//
// signalTypeOf is constexpr, so the exhaustive mapping is also pinned at
// compile time via static_assert.
#include "ida/InputKind.h"
#include "ida/SignalType.h"

#include <catch2/catch_test_macros.hpp>

using ida::InputKind;
using ida::SignalType;
using ida::signalTypeOf;

static_assert (signalTypeOf (InputKind::Audio) == SignalType::Audio);
static_assert (signalTypeOf (InputKind::Midi)  == SignalType::Midi);
static_assert (signalTypeOf (InputKind::Video) == SignalType::Video);
static_assert (signalTypeOf (InputKind::Control)             == SignalType::File);
static_assert (signalTypeOf (InputKind::ParameterAutomation) == SignalType::File);
static_assert (signalTypeOf (InputKind::Transport)           == SignalType::File);
static_assert (signalTypeOf (InputKind::System)              == SignalType::File);

TEST_CASE ("signalTypeOf passes the three modality-native kinds straight through",
           "[signal-type]")
{
    CHECK (signalTypeOf (InputKind::Audio) == SignalType::Audio);
    CHECK (signalTypeOf (InputKind::Midi)  == SignalType::Midi);
    CHECK (signalTypeOf (InputKind::Video) == SignalType::Video);
}

TEST_CASE ("signalTypeOf collapses Control / ParameterAutomation / Transport / System to File",
           "[signal-type]")
{
    // V7 alignment plan M2 Risks note line 256: parameter tapes are JSONL
    // files in IAF, so the four non-modality kinds collapse to File for
    // now. If this assertion ever needs to relax, the M11 IAF design is
    // the milestone that earns the change.
    CHECK (signalTypeOf (InputKind::Control)             == SignalType::File);
    CHECK (signalTypeOf (InputKind::ParameterAutomation) == SignalType::File);
    CHECK (signalTypeOf (InputKind::Transport)           == SignalType::File);
    CHECK (signalTypeOf (InputKind::System)              == SignalType::File);
}
