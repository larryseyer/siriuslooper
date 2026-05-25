// E2E round-trip: addBus + setBusMonitorMode(On) → exportGraphState →
// serializeMixerGraphState (JSON) → deserialize → importGraphState into a
// fresh InputMixer with a fresh OutputMixer attached. Bus MON state and
// the auto-minted OutputMixer channel must survive the full disk cycle.
// Mirror of the OutputMixer aux-bus pan/width/gain/muted JSON round-trip
// added in commit 2da5459.

#include "ida/InputMixer.h"
#include "ida/OutputMixer.h"
#include "ida/SessionFormat.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE ("InputMixer bus monitorMode survives the full JSON round-trip",
           "[input-mixer][bus-monitor][persistence][json]")
{
    ida::InputMixer  src;
    ida::OutputMixer srcOut;
    src.attachOutputMixer (&srcOut);
    const auto monBus = src.addBus (ida::BusConfig { 2, "OnBus",  ida::BusKind::Bus });
    const auto offBus = src.addBus (ida::BusConfig { 2, "OffBus", ida::BusKind::Bus });
    src.setBusMonitorMode (monBus, ida::MonitorMode::On);

    const auto exported = src.exportGraphState();
    const auto wireText = ida::persistence::serializeMixerGraphState (exported);
    const auto reloaded = ida::persistence::deserializeInputMixerGraphState (wireText);

    ida::InputMixer  dst;
    ida::OutputMixer dstOut;
    dst.attachOutputMixer (&dstOut);
    dst.importGraphState (reloaded);

    CHECK (dst.busMonitorMode (monBus) == ida::MonitorMode::On);
    CHECK (dst.busMonitorMode (offBus) == ida::MonitorMode::Off);
    CHECK (dst.busMonitorOutputChannel (monBus).has_value());
    CHECK_FALSE (dst.busMonitorOutputChannel (offBus).has_value());
}
