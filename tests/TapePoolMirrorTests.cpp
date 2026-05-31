#include <catch2/catch_test_macros.hpp>
#include "ida/TapePool.h"
#include "ida/TapePoolMirror.h"
#include "ida/InputMixer.h"

TEST_CASE ("mirrorTapePool registers every non-primary pool tape in the mixer", "[tape-pool][mirror]")
{
    ida::TapePool pool;
    const auto first = pool.add ("Tape 1"); // pool starts empty; this is the front
    const auto drums = pool.add ("Drums");
    const auto vox   = pool.add ("Vox");

    ida::InputMixer mixer;         // ctor seeds the primary tape terminal only
    REQUIRE (mixer.tapeCount() == 1);

    ida::mirrorTapePool (pool, mixer);

    CHECK (mixer.tapeCount() == 3);
    REQUIRE (pool.primary().has_value());
    CHECK (*pool.primary() == first);
    CHECK (mixer.hasTape (*pool.primary()));
    CHECK (mixer.hasTape (drums));
    CHECK (mixer.hasTape (vox));
}

TEST_CASE ("mirrorTapePool is idempotent — second call does not double-count", "[tape-pool][mirror]")
{
    ida::TapePool pool;
    pool.add ("First");
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
    pool.add ("Tape 1");                 // id 1 — mirrors the mixer's pinned primary
    const auto drums = pool.add ("Drums"); // id 2 — a removable, non-primary tape
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
    REQUIRE (pool.primary().has_value());
    CHECK (mixer.channelMainOutIsTape (ch, *pool.primary()));
    CHECK_FALSE (mixer.hasTape (drums));
}

TEST_CASE ("the mixer primary tape is protected (the pool floor is not)", "[tape-pool][mirror]")
{
    // The pool floor is overturned (blank-slate spec): a pool may be emptied,
    // so removing its only tape now succeeds. The mixer's pinned primary
    // terminal (TapeId{1}) is a separate, still-permanent concern (Slice 4
    // unpins it); removing it from the mixer is still refused here.
    ida::TapePool pool;
    pool.add ("Tape 1");
    CHECK (pool.remove (ida::TapeId { 1 }));            // pool may now empty
    CHECK (pool.count() == 0);

    ida::InputMixer mixer;
    CHECK_FALSE (mixer.removeTape (ida::TapeId { 1 })); // mixer primary still permanent
}
