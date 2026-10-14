#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace inkstow::portable
{

// Paths and note text at this boundary are UTF-8. Relative paths use '/' and
// must name visible Markdown files; byte snapshots are opaque (including BOM).
class Vault
{
public:
    static std::optional<Vault> open(const std::string& folder, std::string* error);
    const std::filesystem::path& root() const { return mRoot; }
    bool listNotes(std::vector<std::string>* notes, std::string* error) const;
    bool readNote(const std::string& relative, std::string* bytes, std::string* error) const;
    // Recreate a missing note in an existing folder, using the supplied opaque bytes
    // (including any BOM). Refuses existing destinations; a failed write may leave
    // a partial new note. Path-swap limitations are described in README.
    bool createNoteWithContent(
        const std::string& relative, const std::string& bytes, std::string* error) const;
    // Best-effort comparison, not a cross-process transaction. See README.
    bool saveNote(
        const std::string& relative,
        const std::string& expected,
        const std::string& bytes,
        std::string* error) const;

private:
    explicit Vault(std::filesystem::path root)
        : mRoot(std::move(root))
    {
    }
    std::optional<std::filesystem::path> notePath(
        const std::string& relative, std::string* error) const;
    std::filesystem::path mRoot;
};

} // namespace inkstow::portable
