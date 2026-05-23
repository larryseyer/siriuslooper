// Tests for ProcessingChain — the abstract per-SignalType processing
// hierarchy added in M3 Session 1. M5 Session 1 supersedes the M3-era
// `AudioChain` with `ChannelStrip<SignalType::Audio>` (real gain/pan DSP)
// per plan amendment §3; the Midi/Video/File chains remain no-op stubs
// until M9 / M12 / M13.
#include "sirius/ChannelStrip.h"
#include "sirius/ProcessingChain.h"
#include "sirius/SignalType.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using ida::ChannelStrip;
using ida::FileChain;
using ida::MidiChain;
using ida::ProcessingChain;
using ida::SignalType;
using ida::VideoChain;
using ida::makeProcessingChain;

TEST_CASE ("each concrete chain reports its SignalType via the virtual interface",
           "[processing-chain]")
{
    const ChannelStrip<SignalType::Audio> a;
    const MidiChain  m;
    const VideoChain v;
    const FileChain  f;

    CHECK (a.signalType() == SignalType::Audio);
    CHECK (m.signalType() == SignalType::Midi);
    CHECK (v.signalType() == SignalType::Video);
    CHECK (f.signalType() == SignalType::File);
}

TEST_CASE ("makeProcessingChain returns a concrete subclass matching the SignalType",
           "[processing-chain][factory]")
{
    SECTION ("Audio")
    {
        auto chain = makeProcessingChain (SignalType::Audio);
        REQUIRE (chain != nullptr);
        CHECK (chain->signalType() == SignalType::Audio);
        CHECK (dynamic_cast<ChannelStrip<SignalType::Audio>*> (chain.get()) != nullptr);
    }
    SECTION ("Midi")
    {
        auto chain = makeProcessingChain (SignalType::Midi);
        REQUIRE (chain != nullptr);
        CHECK (chain->signalType() == SignalType::Midi);
        CHECK (dynamic_cast<MidiChain*> (chain.get()) != nullptr);
    }
    SECTION ("Video")
    {
        auto chain = makeProcessingChain (SignalType::Video);
        REQUIRE (chain != nullptr);
        CHECK (chain->signalType() == SignalType::Video);
        CHECK (dynamic_cast<VideoChain*> (chain.get()) != nullptr);
    }
    SECTION ("File")
    {
        auto chain = makeProcessingChain (SignalType::File);
        REQUIRE (chain != nullptr);
        CHECK (chain->signalType() == SignalType::File);
        CHECK (dynamic_cast<FileChain*> (chain.get()) != nullptr);
    }
}

TEST_CASE ("base destructor is virtual so unique_ptr<ProcessingChain> deletes correctly",
           "[processing-chain]")
{
    // If ~ProcessingChain were non-virtual, this would leak / undefined-behave
    // when the unique_ptr is destroyed. Compiles + runs clean = invariant holds.
    std::unique_ptr<ProcessingChain> chain = std::make_unique<ChannelStrip<SignalType::Audio>>();
    chain.reset();
    SUCCEED ("virtual destructor exercised");
}
