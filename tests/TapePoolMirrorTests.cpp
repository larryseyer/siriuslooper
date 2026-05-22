#include <catch2/catch_test_macros.hpp>
#include "sirius/TapePool.h"
#include "sirius/TapePoolMirror.h"
#include "sirius/InputMixer.h"

TEST_CASE ("mirrorTapePool registers every non-primary pool tape in the mixer", "[tape-pool][mirror]")
{
    sirius::TapePool pool;            // seeds TapeId{1} "Tape 1"
    const auto drums = pool.add ("Drums");
    const auto vox   = pool.add ("Vox");

    sirius::InputMixer mixer;         // ctor seeds the primary tape terminal only
    REQUIRE (mixer.tapeCount() == 1);

    sirius::mirrorTapePool (pool, mixer);

    CHECK (mixer.tapeCount() == 3);
    CHECK (mixer.hasTape (pool.primary()));
    CHECK (mixer.hasTape (drums));
    CHECK (mixer.hasTape (vox));
}

TEST_CASE ("mirrorTapePool is idempotent — second call does not double-count", "[tape-pool][mirror]")
{
    sirius::TapePool pool;
    pool.add ("A");
    pool.add ("B");

    sirius::InputMixer mixer;
    sirius::mirrorTapePool (pool, mixer);
    const int afterFirst = mixer.tapeCount();

    sirius::mirrorTapePool (pool, mixer); // second call — must be a no-op
    CHECK (mixer.tapeCount() == afterFirst);
}

TEST_CASE ("removing a pooled tape re-mirrors to a consistent mixer", "[tape-pool][mirror]")
{
    sirius::TapePool pool;
    const auto drums = pool.add ("Drums");
    sirius::InputMixer mixer;
    sirius::mirrorTapePool (pool, mixer);
    const auto ch = mixer.addChannel (sirius::InputId (0), sirius::SignalType::Audio);
    REQUIRE (mixer.setChannelMainOutToTape (ch, drums));

    // The MainComponent remove sequence, modelled headlessly: route dependents to
    // primary, drop the mixer terminal, drop the pool entry.
    if (mixer.channelMainOutIsTape (ch, drums))
        mixer.setChannelMainOutToTape (ch);          // fall back to primary
    REQUIRE (mixer.removeTape (drums));
    REQUIRE (pool.remove (drums));

    CHECK (pool.count() == 1);
    CHECK (mixer.tapeCount() == 1);
    CHECK (mixer.channelMainOutIsTape (ch, sirius::TapeId { 1 }));
    CHECK_FALSE (mixer.hasTape (drums));
}

TEST_CASE ("the pool floor and primary tape are protected", "[tape-pool][mirror]")
{
    sirius::TapePool pool;                 // one tape
    CHECK_FALSE (pool.remove (sirius::TapeId { 1 })); // >=1 floor refuses
    sirius::InputMixer mixer;
    CHECK_FALSE (mixer.removeTape (sirius::TapeId { 1 })); // primary is permanent
}
