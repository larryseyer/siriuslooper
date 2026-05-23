#include "ida/SharedMemoryRegion.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace sirius
{

namespace
{
    /// Normalise the caller's `name` into a leading-slash form the POSIX
    /// shm API expects on every supported platform. The class stores the
    /// normalised form so shm_unlink in the destructor matches shm_open.
    std::string normaliseShmName (std::string raw)
    {
        if (raw.empty() || raw.front() != '/')
            raw.insert (raw.begin(), '/');
        return raw;
    }

    [[noreturn]] void throwErrno (const char* prefix, int e)
    {
        throw std::runtime_error (std::string (prefix) + ": " + std::strerror (e));
    }
}

SharedMemoryRegion::SharedMemoryRegion (std::string name, std::size_t bytes, Mode mode)
    : name_ (normaliseShmName (std::move (name)))
{
    const int oflag = (mode == Mode::CreateExclusive)
                          ? (O_RDWR | O_CREAT | O_EXCL)
                          : O_RDWR;
    const mode_t fmode = (mode == Mode::CreateExclusive) ? 0600 : 0;

    fd_ = ::shm_open (name_.c_str(), oflag, fmode);
    if (fd_ < 0)
        throwErrno ("SharedMemoryRegion::shm_open", errno);

    if (mode == Mode::CreateExclusive)
    {
        owns_ = true;
        if (bytes == 0)
        {
            teardown();
            throw std::invalid_argument (
                "SharedMemoryRegion: bytes must be positive when creating");
        }
        if (::ftruncate (fd_, static_cast<off_t> (bytes)) != 0)
        {
            const int e = errno;
            teardown();
            throwErrno ("SharedMemoryRegion::ftruncate", e);
        }
    }

    // Re-stat unconditionally — on macOS the kernel rounds the segment up
    // to a page boundary (16 KiB on Apple Silicon), so the requested
    // `bytes` and the actual mapped size differ. Both sides report the
    // actual size; callers that need an exact byte count check
    // `size() >= requested`.
    {
        struct stat sb {};
        if (::fstat (fd_, &sb) != 0)
        {
            const int e = errno;
            teardown();
            throwErrno ("SharedMemoryRegion::fstat", e);
        }
        bytes_ = static_cast<std::size_t> (sb.st_size);
    }

    mapping_ = ::mmap (nullptr, bytes_, PROT_READ | PROT_WRITE,
                       MAP_SHARED, fd_, 0);
    if (mapping_ == MAP_FAILED)
    {
        const int e = errno;
        mapping_ = nullptr;
        teardown();
        throwErrno ("SharedMemoryRegion::mmap", e);
    }
}

SharedMemoryRegion::~SharedMemoryRegion()
{
    teardown();
}

SharedMemoryRegion::SharedMemoryRegion (SharedMemoryRegion&& other) noexcept
    : name_   (std::move (other.name_))
    , bytes_  (other.bytes_)
    , fd_     (other.fd_)
    , mapping_(other.mapping_)
    , owns_   (other.owns_)
{
    other.bytes_   = 0;
    other.fd_      = -1;
    other.mapping_ = nullptr;
    other.owns_    = false;
}

SharedMemoryRegion& SharedMemoryRegion::operator= (SharedMemoryRegion&& other) noexcept
{
    if (this != &other)
    {
        teardown();
        name_    = std::move (other.name_);
        bytes_   = other.bytes_;
        fd_      = other.fd_;
        mapping_ = other.mapping_;
        owns_    = other.owns_;
        other.bytes_   = 0;
        other.fd_      = -1;
        other.mapping_ = nullptr;
        other.owns_    = false;
    }
    return *this;
}

void SharedMemoryRegion::teardown() noexcept
{
    if (mapping_ != nullptr)
    {
        ::munmap (mapping_, bytes_);
        mapping_ = nullptr;
    }
    if (fd_ >= 0)
    {
        ::close (fd_);
        fd_ = -1;
    }
    if (owns_ && ! name_.empty())
    {
        ::shm_unlink (name_.c_str());
        owns_ = false;
    }
    bytes_ = 0;
}

} // namespace sirius
