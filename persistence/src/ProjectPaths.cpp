#include "ida/ProjectPaths.h"

namespace ida::persistence
{

namespace
{
    constexpr const char* kTapeFileExtension = ".idatape";
    constexpr const char* kTapeStemPrefix    = "tape_";
}

std::string tapeFileName (TapeId id)
{
    return std::string (kTapeStemPrefix) + std::to_string (id.value()) + kTapeFileExtension;
}

juce::File projectTapesDir (const juce::File& appSupportRoot, const IdaProject& project)
{
    return appSupportRoot.getChildFile (juce::String (project.folderId()));
}

juce::File tapeFileFor (const juce::File& appSupportRoot, const IdaProject& project, TapeId id)
{
    return projectTapesDir (appSupportRoot, project)
        .getChildFile (juce::String (tapeFileName (id)));
}

} // namespace ida::persistence
