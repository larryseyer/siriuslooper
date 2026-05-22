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
    CHECK (mixer.hasTape (sirius::TapeId { 1 }));
    CHECK (mixer.hasTape (drums));
    CHECK (mixer.hasTape (vox));
}
