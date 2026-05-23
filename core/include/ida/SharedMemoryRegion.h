#pragma once

#include <cstddef>
#include <string>

namespace sirius
{

/// RAII wrapper over a POSIX shared-memory region (`shm_open` + `mmap` +
/// `ftruncate`). Created by the producer (engine side), opened by the
/// consumer (host child) against the same `name`. Destruction unmaps the
/// region; the creator also `shm_unlink`s so the kernel reclaims the
/// segment.
///
/// POSIX-only — macOS + Linux. Windows defers to the platform-completion
/// roadmap (M23+), matching the M7-era "macOS → iOS → Windows → Linux"
/// platform-order rule.
///
/// **macOS quirk:** `shm_open` names are truncated at 31 bytes including
/// the leading slash, so callers that pass long instance ids should hash
/// or shorten them before constructing. `bytes` must be greater than zero.
class SharedMemoryRegion
{
public:
    enum class Mode
    {
        /// Owner: shm_open with O_CREAT|O_EXCL, ftruncate to `bytes`,
        /// mmap RW. Destruction shm_unlinks the name.
        CreateExclusive,

        /// Consumer: shm_open without O_CREAT, mmap RW against the
        /// existing segment. Destruction does NOT unlink.
        OpenExisting
    };

    /// Opens (or creates) the segment. On failure throws std::runtime_error
    /// with the underlying errno text — the caller surfaces that as a
    /// missing-IPC condition. `bytes` ignored for OpenExisting (the kernel
    /// already knows the size).
    SharedMemoryRegion (std::string name, std::size_t bytes, Mode mode);

    ~SharedMemoryRegion();

    SharedMemoryRegion (const SharedMemoryRegion&)            = delete;
    SharedMemoryRegion& operator= (const SharedMemoryRegion&) = delete;

    SharedMemoryRegion (SharedMemoryRegion&&) noexcept;
    SharedMemoryRegion& operator= (SharedMemoryRegion&&) noexcept;

    /// Pointer to the start of the mapped region.
    void* data() noexcept { return mapping_; }

    /// Mapped byte count. May exceed the `bytes` arg passed to
    /// CreateExclusive — POSIX implementations round shm segments up to
    /// a page boundary (16 KiB on Apple Silicon, typically 4 KiB on
    /// Linux). Callers that need an exact size check `size() >= requested`.
    std::size_t size() const noexcept { return bytes_; }

    /// The shm name the segment was created/opened with (without the
    /// leading slash if the caller passed one — kept verbatim).
    const std::string& name() const noexcept { return name_; }

private:
    void teardown() noexcept;

    std::string name_;
    std::size_t bytes_   { 0 };
    int         fd_      { -1 };
    void*       mapping_ { nullptr };
    bool        owns_    { false }; ///< true → shm_unlink on destruction
};

} // namespace sirius
