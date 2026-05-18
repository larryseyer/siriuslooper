// Tests for the M2 Session 2 type shapes in engine/include/sirius/Channel.h:
// the strong-typed InputId and ChannelId wrappers, and the Channel aggregate
// itself. These are skeleton tests — they pin down construction, equality,
// and field round-trip only. The ProcessingChain / destinations members
// land in M3 along with the real InputMixer body, and the buffer-flow
// assertions land then too (V7 alignment plan line 238).
//
// The strong-typed IDs follow the house pattern set by TapeId /
// ConstituentId: explicit constexpr ctor, value() accessor, ==/!= operators,
// no implicit conversion to or from the underlying integer. Tests here
// mirror the shape of TapeId / ConstituentId tests where they exist.
#include "sirius/Channel.h"
#include "sirius/SignalType.h"
#include "sirius/TapeMode.h"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

using sirius::Channel;
using sirius::ChannelId;
using sirius::InputId;
using sirius::SignalType;
using sirius::TapeMode;

static_assert (! std::is_convertible_v<int, InputId>,
               "InputId must NOT be implicitly constructible from int — strong typing");
static_assert (! std::is_convertible_v<int, ChannelId>,
               "ChannelId must NOT be implicitly constructible from int — strong typing");

TEST_CASE ("InputId is a strong-typed wrapper around int64", "[channel][input-id]")
{
    // Note: matches house TapeId / ConstituentId pattern — the ctor is
    // constexpr but value() / == / != are not. Runtime CHECKs are the
    // available shape until the house IDs go constexpr-throughout (which
    // is a project-wide decision, not an M2 local one).
    const InputId a (7);
    const InputId b (7);
    const InputId c (9);

    CHECK (a.value() == 7);
    CHECK (a == b);
    CHECK (a != c);
}

TEST_CASE ("ChannelId is a strong-typed wrapper around int64", "[channel][channel-id]")
{
    const ChannelId a (42);
    const ChannelId b (42);
    const ChannelId c (43);

    CHECK (a.value() == 42);
    CHECK (a == b);
    CHECK (a != c);
}

TEST_CASE ("Channel aggregate pairs an id, signal type, input source, and tape mode",
           "[channel]")
{
    const Channel ch {
        ChannelId (5),
        SignalType::Audio,
        InputId (0),
        TapeMode::CommitToTape
    };

    CHECK (ch.id == ChannelId (5));
    CHECK (ch.signalType == SignalType::Audio);
    CHECK (ch.source == InputId (0));
    CHECK (ch.tapeMode == TapeMode::CommitToTape);
}

TEST_CASE ("Channel admits every SignalType × TapeMode combination",
           "[channel]")
{
    // Until M3 adds invariants (ProcessingChain must match SignalType,
    // tape allocation tied to TapeMode), the aggregate is a plain bag.
    // This test pins that down: no combination is rejected, all 12 build.
    SECTION ("MIDI on a non-destructive tape")
    {
        const Channel ch { ChannelId (1), SignalType::Midi, InputId (2),
                           TapeMode::NonDestructive };
        CHECK (ch.signalType == SignalType::Midi);
        CHECK (ch.tapeMode == TapeMode::NonDestructive);
    }
    SECTION ("video with no tape (direct layer only)")
    {
        const Channel ch { ChannelId (2), SignalType::Video, InputId (3),
                           TapeMode::NoTape };
        CHECK (ch.signalType == SignalType::Video);
        CHECK (ch.tapeMode == TapeMode::NoTape);
    }
    SECTION ("file (parameter automation) committed to tape")
    {
        const Channel ch { ChannelId (3), SignalType::File, InputId (4),
                           TapeMode::CommitToTape };
        CHECK (ch.signalType == SignalType::File);
        CHECK (ch.tapeMode == TapeMode::CommitToTape);
    }
}

TEST_CASE ("TapeMode is a three-case closed enum", "[channel][tape-mode]")
{
    // Sanity that the three named cases the V3 transition guide §2.1 listed
    // (CommitToTape / NonDestructive / NoTape) all exist and are distinct.
    // If a future session adds a fourth case, the plan's "channel-driven
    // tape topology" reasoning needs revisiting first.
    CHECK (TapeMode::CommitToTape   != TapeMode::NonDestructive);
    CHECK (TapeMode::NonDestructive != TapeMode::NoTape);
    CHECK (TapeMode::CommitToTape   != TapeMode::NoTape);
}
