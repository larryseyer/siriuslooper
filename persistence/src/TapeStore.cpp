#include "sirius/TapeStore.h"

#include <juce_cryptography/juce_cryptography.h>

#include <stdexcept>

namespace sirius::persistence
{

namespace
{
    constexpr const char* tapeFileExtension = ".tape";

    juce::String hashOf (const juce::MemoryBlock& bytes)
    {
        return juce::SHA256 (bytes.getData(), bytes.getSize()).toHexString();
    }
}

TapeStore::TapeStore (juce::File directory)
    : directory_ (std::move (directory))
{
    const auto created = directory_.createDirectory();
    if (created.failed())
        throw std::runtime_error ("sirius::persistence::TapeStore: cannot create directory: "
                                  + created.getErrorMessage().toStdString());
    if (! directory_.hasWriteAccess())
        throw std::runtime_error ("sirius::persistence::TapeStore: directory is not writable: "
                                  + directory_.getFullPathName().toStdString());
}

juce::String TapeStore::store (const juce::MemoryBlock& bytes)
{
    const auto hash = hashOf (bytes);
    const auto file = fileFor (hash);

    // Idempotent: identical content always lands at the same path, and a tape
    // file is never rewritten once present — the data layer is immutable.
    if (file.existsAsFile())
        return hash;

    if (! file.replaceWithData (bytes.getData(), bytes.getSize()))
        throw std::runtime_error ("sirius::persistence::TapeStore: write failed: "
                                  + file.getFullPathName().toStdString());

    return hash;
}

bool TapeStore::exists (const juce::String& contentHash) const
{
    return fileFor (contentHash).existsAsFile();
}

bool TapeStore::read (const juce::String& contentHash, juce::MemoryBlock& out) const
{
    const auto file = fileFor (contentHash);
    if (! file.existsAsFile())
        return false;

    if (! file.loadFileAsData (out))
        throw std::runtime_error ("sirius::persistence::TapeStore: read failed: "
                                  + file.getFullPathName().toStdString());
    return true;
}

juce::File TapeStore::fileFor (const juce::String& contentHash) const
{
    return directory_.getChildFile (contentHash + tapeFileExtension);
}

} // namespace sirius::persistence
