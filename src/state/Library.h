#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "PathResolve.h"

namespace namrig::state
{

// The user's sound library and the machine-level settings that locate it.
// Message thread only.
//
// Layout (docs/plan.md, decided with the user):
//   <library>/Models/   .nam collection — owned by this plugin's world
//   <library>/Presets/  rig presets (JSON, portable paths)
//   <ir root>           the user's EXISTING IR collection, shared with other
//                       plugins — we only point at it, never organize it
//
// Machine settings (library location, IR root) live in ~/.config/NamRig.settings,
// shared by the standalone and the CLAP. Presets are the sound; settings are
// the machine.
class Library
{
public:
    Library();

    juce::File libraryDir() const;          // default ~/Music/NAM Rig
    juce::File modelsDir() const { return libraryDir().getChildFile("Models"); }
    juce::File presetsDir() const { return libraryDir().getChildFile("Presets"); }
    juce::File irRoot() const;              // empty until set/seeded

    void setLibraryDir(const juce::File&);
    void setIrRoot(const juce::File&);
    // Adopt the folder of the first-loaded IR as the IR root if none is set.
    void seedIrRootFrom(const juce::File& irFile);

    // Preset files (no extension in the names shown to the user).
    juce::StringArray listPresets() const;
    juce::File presetFile(const juce::String& name) const;

    // Portable path helpers bridging juce::File <-> StoredPath.
    StoredPath storeModelPath(const juce::File&) const;
    StoredPath storeIrPath(const juce::File&) const;
    // Resolved absolute path, or empty. `usedSearch` = found by filename scan.
    juce::File resolveModelPath(const StoredPath&, bool* usedSearch = nullptr) const;
    juce::File resolveIrPath(const StoredPath&, bool* usedSearch = nullptr) const;

    // StoredPath <-> juce::var (for preset JSON and session state).
    static juce::var toVar(const StoredPath&);
    static StoredPath fromVar(const juce::var&);

private:
    std::unique_ptr<juce::PropertiesFile> settings;
};

} // namespace namrig::state
