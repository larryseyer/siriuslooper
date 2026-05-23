#include <catch2/catch_test_macros.hpp>
#include "ida/TapePool.h"
#include "ida/TapePoolMirror.h"
#include "ida/InputMixer.h"

TEST_CASE ("mirrorTapePool registers every non-primary pool tape in the mixer", "[tape-pool][mirror]")
{
    ida::TapePool pool;            // seeds TapeId{1} "Tape 1"
    const auto drums = pool.add ("Drums");
    const auto vox   = pool.add ("Vox");

    ida::InputMixer mixer;         // ctor seeds the primary tape terminal only
    REQUIRE (mixer.tapeCount() == 1);

    ida::mirrorTapePool (pool, mixer);

    CHECK (mixer.tapeCount() == 3);
    CHECK (mixer.hasTape (pool.primary()));
    CHECK (mixer.hasTape (drums));
    CHECK (mixer.hasTape (vox));
}

TEST_CASE ("mirrorTapePool is idempotent — second call does not double-count", "[tape-pool][mirror]")
{
    ida::TapePool pool;
    pool.add ("A");
    pool.add ("B");

    ida::InputMixer mixer;
    ida::mirrorTapePool (pool, mixer);
    const int afterFirst = mixer.tapeCount();

    ida::mirrorTapePool (pool, mixer); // second call — must be a no-op
    CHECK (mixer.tapeCount() == afterFirst);
}

TEST_CASE ("removing a pooled tape re-mirrors to a consistent mixer", "[tape-pool][mirror]")
{
    ida::TapePool pool;
    const auto drums = pool.add ("Drums");
    ida::InputMixer mixer;
    ida::mirrorTapePool (pool, mixer);
    const auto ch = mixer.addChannel (ida::InputId (0), ida::SignalType::Audio);
    REQUIRE (mixer.setChannelMainOutToTape (ch, drums));

    // The MainComponent remove sequence, modelled headlessly: route dependents to
    // primary, drop the mixer terminal, drop the pool entry.
    if (mixer.channelMainOutIsTape (ch, drums))
        mixer.setChannelMainOutToTape (ch);          // fall back to primary
    REQUIRE (mixer.removeTape (drums));
    REQUIRE (pool.remove (drums));

    CHECK (pool.count() == 1);
    CHECK (mixer.tapeCount() == 1);
    CHECK (mixer.channelMainOutIsTape (ch, pool.primary()));
    CHECK_FALSE (mixer.hasTape (drums));
}

TEST_CASE ("the pool floor and primary tape are protected", "[tape-pool][mirror]")
{
    ida::TapePool pool;                 // one tape
    CHECK_FALSE (pool.remove (ida::TapeId { 1 })); // >=1 floor refuses
    ida::InputMixer mixer;
    CHECK_FALSE (mixer.removeTape (ida::TapeId { 1 })); // primary is permanent
}
