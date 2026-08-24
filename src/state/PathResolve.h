#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace namrig::state
{

// Portable path handling for presets and session state. A stored reference
// keeps three degrees of freedom, tried in order at resolve time:
//   1. relative path against the current root   (library moved wholesale)
//   2. absolute path                            (nothing moved)
//   3. filename search under the root           (files reorganized)
// Pure std::filesystem — no JUCE — so it's testable in the engine suite.
struct StoredPath
{
    std::string relative; // empty when the file wasn't under the root at save
    std::string absolute;
    std::string filename;

    bool empty() const { return absolute.empty() && filename.empty(); }
};

// Build a stored reference for `file` against `root` (root may be empty).
StoredPath makeStoredPath(const std::filesystem::path& file, const std::filesystem::path& root);

// Resolve against the current root. nullopt = not found anywhere.
// `usedSearch` (optional out) reports that the file was found by filename
// search rather than by its stored location — worth a UI note.
std::optional<std::filesystem::path> resolveStoredPath(const StoredPath& stored,
                                                       const std::filesystem::path& root,
                                                       bool* usedSearch = nullptr);

// First file named `filename` under `root` (recursive, depth-limited).
std::optional<std::filesystem::path> findByFilename(const std::filesystem::path& root,
                                                    const std::string& filename,
                                                    int maxDepth = 6);

} // namespace namrig::state
