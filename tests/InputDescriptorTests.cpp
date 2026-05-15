// Tests for sirius::InputDescriptor — the free-standing metadata struct
// that pairs a TapeId with the human-visible shape of an input (kind,
// display name, optional channel/port index). Per the white paper's §7.2
// data-layer / structure-layer split, Tape<T> itself does not know about
// descriptors; the descriptor points *at* a tape by id. These tests pin
// down construction, equality of the underlying fields, and the optional-
// channel semantics for kinds that have no channel concept (Transport,
// System).
#include "sirius/InputDescriptor.h"
#include "sirius/InputKind.h"
#include "sirius/TapeId.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

using sirius::InputDescriptor;
using sirius::InputKind;
using sirius::TapeId;

TEST_CASE ("audio input descriptor carries its tape id, kind, name, and channel index",
           "[input-descriptor]")
{
    const InputDescriptor d {
        TapeId (3),
        InputKind::Audio,
        std::string ("Guitar L"),
        std::optional<int> (0)
    };

    CHECK (d.tapeId == TapeId (3));
    CHECK (d.inputKind == InputKind::Audio);
    CHECK (d.displayName == "Guitar L");
    REQUIRE (d.channelOrPortIndex.has_value());
    CHECK (*d.channelOrPortIndex == 0);
}

TEST_CASE ("midi input descriptor carries a port index",
           "[input-descriptor]")
{
    const InputDescriptor d {
        TapeId (8),
        InputKind::Midi,
        std::string ("MPK mini"),
        std::optional<int> (2)
    };

    CHECK (d.inputKind == InputKind::Midi);
    REQUIRE (d.channelOrPortIndex.has_value());
    CHECK (*d.channelOrPortIndex == 2);
}

TEST_CASE ("transport and system descriptors have no channel or port index",
           "[input-descriptor]")
{
    SECTION ("transport")
    {
        const InputDescriptor d {
            TapeId (100),
            InputKind::Transport,
            std::string ("Session transport"),
            std::nullopt
        };
        CHECK (d.inputKind == InputKind::Transport);
        CHECK_FALSE (d.channelOrPortIndex.has_value());
    }

    SECTION ("system")
    {
        const InputDescriptor d {
            TapeId (101),
            InputKind::System,
            std::string ("LMC discipline events"),
            std::nullopt
        };
        CHECK (d.inputKind == InputKind::System);
        CHECK_FALSE (d.channelOrPortIndex.has_value());
    }
}

TEST_CASE ("parameter-automation, control, and video descriptors round-trip their fields",
           "[input-descriptor]")
{
    SECTION ("parameter automation")
    {
        const InputDescriptor d {
            TapeId (42),
            InputKind::ParameterAutomation,
            std::string ("Filter Cutoff"),
            std::optional<int> (12)
        };
        CHECK (d.tapeId == TapeId (42));
        CHECK (d.inputKind == InputKind::ParameterAutomation);
        CHECK (d.displayName == "Filter Cutoff");
        REQUIRE (d.channelOrPortIndex.has_value());
        CHECK (*d.channelOrPortIndex == 12);
    }

    SECTION ("control surface")
    {
        const InputDescriptor d {
            TapeId (50),
            InputKind::Control,
            std::string ("Footswitch A"),
            std::optional<int> (1)
        };
        CHECK (d.inputKind == InputKind::Control);
        CHECK (*d.channelOrPortIndex == 1);
    }

    SECTION ("video camera")
    {
        const InputDescriptor d {
            TapeId (200),
            InputKind::Video,
            std::string ("Front camera"),
            std::optional<int> (0)
        };
        CHECK (d.inputKind == InputKind::Video);
        CHECK (*d.channelOrPortIndex == 0);
    }
}

TEST_CASE ("display name preserves whitespace and unicode",
           "[input-descriptor]")
{
    const InputDescriptor d {
        TapeId (1),
        InputKind::Audio,
        std::string ("  Vocal — Lead  "),
        std::optional<int> (0)
    };
    CHECK (d.displayName == "  Vocal — Lead  ");
}
