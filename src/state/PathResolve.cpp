#include "PathResolve.h"

#include <system_error>

namespace namrig::state
{

namespace fs = std::filesystem;

StoredPath makeStoredPath(const fs::path& file, const fs::path& root)
{
    StoredPath stored;
    stored.absolute = file.string();
    stored.filename = file.filename().string();

    if (!root.empty())
    {
        std::error_code ec;
        const fs::path rel = fs::relative(file, root, ec);
        // Reject escapes ("../...") — only keep genuinely-inside paths.
        // (string(), not native(): native() is wide on Windows.)
        if (!ec && !rel.empty() && rel.string().rfind("..", 0) != 0)
            stored.relative = rel.string();
    }
    return stored;
}

std::optional<fs::path> resolveStoredPath(const StoredPath& stored, const fs::path& root,
                                          bool* usedSearch)
{
    if (usedSearch != nullptr)
        *usedSearch = false;

    std::error_code ec;
    if (!stored.relative.empty() && !root.empty())
    {
        const fs::path candidate = root / stored.relative;
        if (fs::is_regular_file(candidate, ec))
            return candidate;
    }
    if (!stored.absolute.empty())
    {
        const fs::path candidate{stored.absolute};
        if (fs::is_regular_file(candidate, ec))
            return candidate;
    }
    if (!stored.filename.empty() && !root.empty())
    {
        if (auto found = findByFilename(root, stored.filename))
        {
            if (usedSearch != nullptr)
                *usedSearch = true;
            return found;
        }
    }
    return std::nullopt;
}

std::optional<fs::path> findByFilename(const fs::path& root, const std::string& filename,
                                       const int maxDepth)
{
    std::error_code ec;
    if (root.empty() || !fs::is_directory(root, ec))
        return std::nullopt;

    auto it = fs::recursive_directory_iterator(
        root, fs::directory_options::skip_permission_denied, ec);
    if (ec)
        return std::nullopt;

    for (const auto end = fs::recursive_directory_iterator{}; it != end; it.increment(ec))
    {
        if (ec)
            return std::nullopt;
        if (it.depth() > maxDepth)
        {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(ec) && it->path().filename().string() == filename)
            return it->path();
    }
    return std::nullopt;
}

} // namespace namrig::state
