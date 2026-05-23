// Tests for ida::SharedMemoryRegion — POSIX shm_open + mmap RAII.

#include "sirius/SharedMemoryRegion.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstring>
#include <random>
#include <stdexcept>
#include <string>

using ida::SharedMemoryRegion;

namespace
{
    /// Per-test unique shm name. macOS truncates at 31 bytes including the
    /// leading slash; keep the prefix short and the suffix bounded.
    std::string uniqueName (const char* suffix)
    {
        // 5-digit hex randomness — fits comfortably inside the 30-char
        // remaining budget after "/sirius-shm-" (12 chars) and the suffix.
        std::random_device rd;
        char rnd[6];
        std::snprintf (rnd, sizeof (rnd), "%05x", rd() & 0xFFFFF);
        return std::string ("/sirius-shm-") + rnd + "-" + suffix;
    }
}

TEST_CASE ("create and open round-trip writes", "[shm-region]")
{
    constexpr std::size_t kBytes = 4096;
    const auto name = uniqueName ("rt");

    {
        SharedMemoryRegion writer (name, kBytes,
                                   SharedMemoryRegion::Mode::CreateExclusive);
        REQUIRE (writer.data() != nullptr);
        // Kernel may round up to page boundary (16 KiB on Apple Silicon).
        REQUIRE (writer.size() >= kBytes);

        SharedMemoryRegion reader (name, 0,
                                   SharedMemoryRegion::Mode::OpenExisting);
        REQUIRE (reader.data() != nullptr);
        REQUIRE (reader.size() == writer.size()); // both must see the same size

        // Writer puts a marker; reader sees it.
        const char marker[] = "sirius-shm-round-trip";
        std::memcpy (writer.data(), marker, sizeof (marker));
        CHECK (std::memcmp (reader.data(), marker, sizeof (marker)) == 0);
    }
    // Both ends destroyed — writer's shm_unlink ran. Re-creating with the
    // same name must succeed (O_EXCL would fail otherwise).
    SharedMemoryRegion rec (name, kBytes, SharedMemoryRegion::Mode::CreateExclusive);
    CHECK (rec.size() >= kBytes);
}

TEST_CASE ("OpenExisting against a missing name fails loud", "[shm-region]")
{
    CHECK_THROWS_AS (SharedMemoryRegion ("/sirius-shm-does-not-exist", 0,
                                         SharedMemoryRegion::Mode::OpenExisting),
                     std::runtime_error);
}

TEST_CASE ("CreateExclusive with zero bytes is rejected", "[shm-region]")
{
    const auto name = uniqueName ("zb");
    CHECK_THROWS_AS (SharedMemoryRegion (name, 0,
                                         SharedMemoryRegion::Mode::CreateExclusive),
                     std::invalid_argument);
    // The throw happens after shm_open succeeded, so the destructor must
    // shm_unlink the empty segment; re-creating with the same name should
    // succeed afterwards. (This test fails if teardown forgot to unlink.)
    SharedMemoryRegion rec (name, 64, SharedMemoryRegion::Mode::CreateExclusive);
    CHECK (rec.size() >= 64);
}
