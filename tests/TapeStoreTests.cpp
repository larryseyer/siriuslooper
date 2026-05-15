// Tests for the content-addressed tape store (white paper Part 7.8). The store
// is the data-layer half of a session directory; these tests pin down the
// claims that make a session safely copyable: identical content always lands
// at the same path, files once written are not rewritten, and an absent tape
// is a "not present" answer rather than an exception.
#include "sirius/TapeStore.h"

#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <stdexcept>

using sirius::persistence::TapeStore;

namespace
{
    /// A unique scratch directory per test, cleaned up on destruction. Using
    /// the harness's actual temp dir keeps the test isolated from the source
    /// tree even when invoked through ctest.
    class ScratchDir
    {
    public:
        ScratchDir()
            : dir_ (juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile ("sirius-tapestore-test-"
                                       + juce::String (juce::Uuid().toString())))
        {
            dir_.deleteRecursively();
        }

        ~ScratchDir() { dir_.deleteRecursively(); }

        const juce::File& file() const { return dir_; }

    private:
        juce::File dir_;
    };

    juce::MemoryBlock blockOf (const char* literal)
    {
        return { literal, std::strlen (literal) };
    }
}

TEST_CASE ("a stored tape can be read back byte-for-byte", "[tapestore]")
{
    ScratchDir scratch;
    TapeStore store (scratch.file());

    const auto payload = blockOf ("the quick brown fox jumps over the lazy dog");
    const auto hash = store.store (payload);
    CHECK (hash.isNotEmpty());
    CHECK (store.exists (hash));

    juce::MemoryBlock readBack;
    REQUIRE (store.read (hash, readBack));
    CHECK (readBack.getSize() == payload.getSize());
    CHECK (std::memcmp (readBack.getData(), payload.getData(), payload.getSize()) == 0);
}

TEST_CASE ("identical content lands at the same hash and does not rewrite",
           "[tapestore]")
{
    // White paper Part 7.8: tape files are content-addressed, written once,
    // never rewritten. Storing the same bytes twice must produce the same hash
    // and leave the existing file's mtime untouched.
    ScratchDir scratch;
    TapeStore store (scratch.file());

    const auto payload = blockOf ("immutable take");
    const auto firstHash = store.store (payload);
    const auto firstWrite = store.fileFor (firstHash).getLastModificationTime();

    juce::Thread::sleep (15); // ensure clock-resolution clears for the next mtime check

    const auto secondHash = store.store (payload);
    CHECK (firstHash == secondHash);
    CHECK (store.fileFor (firstHash).getLastModificationTime() == firstWrite);
}

TEST_CASE ("different content produces different hashes", "[tapestore]")
{
    ScratchDir scratch;
    TapeStore store (scratch.file());

    const auto hashA = store.store (blockOf ("alpha"));
    const auto hashB = store.store (blockOf ("beta"));
    CHECK (hashA != hashB);
    CHECK (store.exists (hashA));
    CHECK (store.exists (hashB));
}

TEST_CASE ("reading a hash that was never stored returns false, not an error",
           "[tapestore]")
{
    // An absent tape is information, not corruption — the caller is allowed to
    // ask. A read failure on a *present* file is the corruption signal and
    // throws instead, but that path is exercised only when the store is
    // damaged externally.
    ScratchDir scratch;
    TapeStore store (scratch.file());

    juce::MemoryBlock out;
    CHECK_FALSE (store.read ("0123456789abcdef0123456789abcdef", out));
    CHECK_FALSE (store.exists ("0123456789abcdef0123456789abcdef"));
}

TEST_CASE ("the store creates its directory if it does not already exist",
           "[tapestore]")
{
    ScratchDir scratch;
    REQUIRE_FALSE (scratch.file().exists());

    TapeStore store (scratch.file());
    CHECK (scratch.file().isDirectory());

    // And operations work on the freshly-minted directory.
    const auto hash = store.store (blockOf ("hello"));
    CHECK (store.exists (hash));
}
