#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace ida::prefs
{

// Shared app-level PropertiesFile. JUCE auto-loads on first construction and
// writes via saveIfNeeded(); the static local makes this a single shared
// instance across translation units (C++17 inline-function static guarantees).
// On macOS the file lives at ~/Library/Application Support/IDA/IDA.settings;
// other platforms get the JUCE-canonical equivalents.
inline juce::PropertiesFile& shared()
{
    static juce::PropertiesFile p ([]
    {
        juce::PropertiesFile::Options o;
        o.applicationName     = "IDA";
        o.filenameSuffix      = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        o.storageFormat       = juce::PropertiesFile::storeAsXML;
        return o;
    }());
    return p;
}

// Last folder the operator picked an audio file from. Shared between the
// "Add file input" picker (MainComponent) and the player window's "+" append.
// Returns juce::File() if unset or stale (directory no longer exists).
inline juce::File lastFileInputFolder()
{
    const auto stored = shared().getValue ("lastFileInputFolder");
    if (stored.isEmpty()) return {};
    juce::File f (stored);
    return f.isDirectory() ? f : juce::File {};
}

inline void setLastFileInputFolder (const juce::File& folder)
{
    if (folder == juce::File {} || ! folder.isDirectory()) return;
    shared().setValue ("lastFileInputFolder", folder.getFullPathName());
    shared().saveIfNeeded();
}

} // namespace ida::prefs
