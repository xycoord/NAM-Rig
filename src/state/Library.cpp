#include "Library.h"

namespace namrig::state
{

namespace
{
std::filesystem::path toStd(const juce::File& f)
{
    return std::filesystem::path{f.getFullPathName().toStdString()};
}
juce::File toJuce(const std::filesystem::path& p)
{
    return juce::File{juce::String{p.string()}};
}
} // namespace

Library::Library()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "NamRig"; // shared by standalone + CLAP
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
#if JUCE_LINUX || JUCE_BSD
    options.folderName = "~/.config";
#else
    options.folderName = "";
#endif
    settings = std::make_unique<juce::PropertiesFile>(options);
}

juce::File Library::libraryDir() const
{
    const juce::String stored = settings->getValue("libraryDir");
    if (stored.isNotEmpty())
        return juce::File{stored};
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory)
        .getChildFile("NAM Rig");
}

juce::File Library::irRoot() const
{
    const juce::String stored = settings->getValue("irRoot");
    return stored.isNotEmpty() ? juce::File{stored} : juce::File{};
}

void Library::setLibraryDir(const juce::File& dir)
{
    settings->setValue("libraryDir", dir.getFullPathName());
    settings->saveIfNeeded();
}

void Library::setIrRoot(const juce::File& dir)
{
    settings->setValue("irRoot", dir.getFullPathName());
    settings->saveIfNeeded();
}

void Library::seedIrRootFrom(const juce::File& irFile)
{
    if (irRoot() == juce::File{} && irFile.existsAsFile())
        setIrRoot(irFile.getParentDirectory());
}

juce::StringArray Library::listPresets() const
{
    juce::StringArray names;
    for (const auto& f : presetsDir().findChildFiles(juce::File::findFiles, false, "*.json"))
        names.add(f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

juce::File Library::presetFile(const juce::String& name) const
{
    return presetsDir().getChildFile(
        juce::File::createLegalFileName(name) + ".json");
}

StoredPath Library::storeModelPath(const juce::File& f) const
{
    return makeStoredPath(toStd(f), toStd(modelsDir()));
}

StoredPath Library::storeIrPath(const juce::File& f) const
{
    return makeStoredPath(toStd(f), toStd(irRoot()));
}

juce::File Library::resolveModelPath(const StoredPath& stored, bool* usedSearch) const
{
    if (auto p = resolveStoredPath(stored, toStd(modelsDir()), usedSearch))
        return toJuce(*p);
    return {};
}

juce::File Library::resolveIrPath(const StoredPath& stored, bool* usedSearch) const
{
    if (auto p = resolveStoredPath(stored, toStd(irRoot()), usedSearch))
        return toJuce(*p);
    return {};
}

juce::var Library::toVar(const StoredPath& p)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("relative", juce::String{p.relative});
    obj->setProperty("absolute", juce::String{p.absolute});
    obj->setProperty("filename", juce::String{p.filename});
    return juce::var{obj};
}

StoredPath Library::fromVar(const juce::var& v)
{
    StoredPath p;
    if (auto* obj = v.getDynamicObject())
    {
        p.relative = obj->getProperty("relative").toString().toStdString();
        p.absolute = obj->getProperty("absolute").toString().toStdString();
        p.filename = obj->getProperty("filename").toString().toStdString();
    }
    return p;
}

} // namespace namrig::state
