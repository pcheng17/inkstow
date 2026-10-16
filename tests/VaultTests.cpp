#include "PortableTestSupport.h"
#include "Session.h"
#include "Vault.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>

#include <cstdlib>
#endif

namespace fs = std::filesystem;
using namespace inkstow::portable;
using namespace inkstow::portable::test;

namespace
{
TEST_CASE("Vault discovers visible fixture notes and refuses unsafe paths")
{
    std::string error;
    auto vault = Vault::open(SAMPLE_VAULT, &error);
    REQUIRE(vault);
    // Static analysis cannot infer that doctest REQUIRE aborts on failure.
    if (!vault) return;
    std::vector<std::string> notes;
    REQUIRE(vault->listNotes(&notes, &error));
    REQUIRE((notes == std::vector<std::string>{"Folder/Second note.md", "Welcome.md"}));
    std::string bytes;
    REQUIRE(vault->readNote("Welcome.md", &bytes, &error));
    REQUIRE(!bytes.empty());
    REQUIRE(!vault->readNote("../Welcome.md", &bytes, &error));
    REQUIRE(!vault->readNote(".obsidian/Hidden.md", &bytes, &error));
    REQUIRE(!vault->readNote("Folder/../Welcome.md", &bytes, &error));
    REQUIRE(!vault->readNote("/Welcome.md", &bytes, &error));
}

TEST_CASE("Vault recovery creates missing notes only in existing folders")
{
    TempVault temp;
    REQUIRE(fs::create_directory(temp.mPath / "Folder"));
    std::string error;
    auto vault = Vault::open(utf8Path(temp.mPath), &error);
    REQUIRE(vault);
    if (!vault) return;
    const std::string bytes = "\xEF\xBB\xBF"
                              "Recovered\n";
    REQUIRE(vault->createNoteWithContent("Folder/Recovered.md", bytes, &error));
    REQUIRE(read(temp.mPath / "Folder" / "Recovered.md") == bytes);
    std::string opened;
    REQUIRE(vault->readNote("Folder/Recovered.md", &opened, &error));
    REQUIRE(opened == bytes);
}

TEST_CASE("Vault recovery never overwrites a reappeared note")
{
    TempVault temp;
    std::string error;
    auto vault = Vault::open(utf8Path(temp.mPath), &error);
    REQUIRE(vault);
    if (!vault) return;
    const fs::path destination = temp.mPath / "Recovered.md";
    write(destination, "outside");
    REQUIRE(!vault->createNoteWithContent("Recovered.md", "mine", &error));
    REQUIRE(error.find("already exists") != std::string::npos);
    REQUIRE(read(destination) == "outside");
    REQUIRE(fs::remove(destination));
    REQUIRE(vault->createNoteWithContent("Recovered.md", "mine", &error));
    REQUIRE(read(destination) == "mine");
    REQUIRE(!vault->createNoteWithContent("Recovered.md", "again", &error));
    REQUIRE(error.find("already exists") != std::string::npos);
    REQUIRE(read(destination) == "mine");
}

TEST_CASE("Vault recovery rejects invalid paths and missing parents")
{
    TempVault temp;
    std::string error;
    auto vault = Vault::open(utf8Path(temp.mPath), &error);
    REQUIRE(vault);
    if (!vault) return;
    const std::vector<std::string> invalid = {
        "../Outside.md",
        ".hidden.md",
        "Folder/.hidden.md",
        "Folder/../Escape.md",
        "/Absolute.md",
        "Folder\\Escape.md",
        "Note.txt",
        "Bad\xC0\xAF.md",
        std::string("Null\0.md", 8)};
    for (const auto& path : invalid) {
        REQUIRE(!vault->createNoteWithContent(path, "mine", &error));
        REQUIRE(error.find("visible relative Markdown path") != std::string::npos);
    }
    error.clear();
    REQUIRE(!vault->createNoteWithContent("Missing/Note.md", "mine", &error));
    REQUIRE(error.find("note parent folder") != std::string::npos);
    REQUIRE(!fs::exists(temp.mPath / "Missing"));
    write(temp.mPath / "File.md", "outside");
    REQUIRE(!vault->createNoteWithContent("File.md/Note.md", "mine", &error));
    REQUIRE(read(temp.mPath / "File.md") == "outside");
}

TEST_CASE("Vault recovery rejects symlink targets and parents")
{
    TempVault temp;
    TempVault other;
    const fs::path outside = other.mPath / "Outside.md";
    write(outside, "outside");
    std::error_code ec;
    fs::create_symlink(outside, temp.mPath / "Linked.md", ec);
    if (ec) return; // symlinks require elevated privileges on some Windows hosts
    fs::create_directory_symlink(other.mPath, temp.mPath / "Alias", ec);
    std::string error;
    auto vault = Vault::open(utf8Path(temp.mPath), &error);
    REQUIRE(vault);
    if (!vault) return;
    REQUIRE(!vault->createNoteWithContent("Linked.md", "mine", &error));
    REQUIRE(error.find("already exists") != std::string::npos);
    if (!ec) {
        REQUIRE(!vault->createNoteWithContent("Alias/Note.md", "mine", &error));
        REQUIRE(!fs::exists(other.mPath / "Note.md"));
    }
    REQUIRE(read(outside) == "outside");
}

#ifdef _WIN32
TEST_CASE("Vault recovery rejects Windows junction parents")
{
    TempVault outside;
    TempVault temp;
    const fs::path junction = temp.mPath / "Junction";
    const fs::path outsideNote = outside.mPath / "Note.md";
    struct JunctionCleanup
    {
        fs::path mPath;
        ~JunctionCleanup()
        {
            std::error_code ignored;
            fs::remove(mPath, ignored);
        }
    } cleanup{junction};
    const std::wstring command =
        L"mklink /J \"" + junction.wstring() + L"\" \"" + outside.mPath.wstring() + L"\"";
    const int result = _wsystem(command.c_str());
    if (result != 0)
        throw std::runtime_error(
            "Cannot create Windows junction with mklink /J (exit code " + std::to_string(result) +
            "): " + utf8Path(junction) + " -> " + utf8Path(outside.mPath));
    const DWORD attributes = GetFileAttributesW(junction.c_str());
    REQUIRE(attributes != INVALID_FILE_ATTRIBUTES);
    REQUIRE((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0);
    REQUIRE((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
    REQUIRE(!fs::exists(outsideNote));
    std::string error;
    auto vault = Vault::open(utf8Path(temp.mPath), &error);
    REQUIRE(vault);
    if (!vault) return;
    REQUIRE(!vault->createNoteWithContent("Junction/Note.md", "mine", &error));
    REQUIRE(error.find("reparse point") != std::string::npos);
    REQUIRE(!fs::exists(outsideNote));
}

#endif

TEST_CASE("Vault refuses symlink paths")
{
    TempVault temp;
    write(temp.mPath / "Good.md", "good");
    std::error_code ec;
    fs::create_symlink(temp.mPath / "Good.md", temp.mPath / "Link.md", ec);
    if (ec) return; // symlinks require elevated privileges on some Windows hosts
    fs::create_directory(temp.mPath / "Folder");
    write(temp.mPath / "Folder" / "Inside.md", "inside");
    fs::create_directory_symlink(temp.mPath / "Folder", temp.mPath / "Alias", ec);
    std::string error;
    auto vault = Vault::open(utf8Path(temp.mPath), &error);
    REQUIRE(vault);
    if (!vault) return;
    std::vector<std::string> notes;
    REQUIRE(vault->listNotes(&notes, &error));
    REQUIRE((notes == std::vector<std::string>{"Folder/Inside.md", "Good.md"}));
    std::string bytes;
    REQUIRE(!vault->readNote("Link.md", &bytes, &error));
    if (!ec) REQUIRE(!vault->readNote("Alias/Inside.md", &bytes, &error));
    REQUIRE(!vault->saveNote("Link.md", "good", "bad", &error));
    REQUIRE(read(temp.mPath / "Good.md") == "good");
}

TEST_CASE("Vault creates notes safely")
{
    TempVault temp;
    std::string error;
    auto vault = Vault::open(utf8Path(temp.mPath), &error);
    REQUIRE(vault);
    if (!vault) return;
    REQUIRE(vault->createNote("Folder/Deep/New.md", &error));
    REQUIRE(read(temp.mPath / "Folder" / "Deep" / "New.md").empty());
    REQUIRE(!vault->createNote("Folder/Deep/New.md", &error));
    REQUIRE(!vault->createNote(".hidden/Note.md", &error));
    REQUIRE(!vault->createNote("Folder/../Escape.md", &error));
    REQUIRE(!fs::exists(temp.mPath / "Escape.md"));
    TempVault outside;
    std::error_code ec;
    fs::create_directory_symlink(outside.mPath, temp.mPath / "Alias", ec);
    if (!ec) {
        REQUIRE(!vault->createNote("Alias/Other.md", &error));
        REQUIRE(!fs::exists(outside.mPath / "Other.md"));
    }
}

TEST_CASE("Vault resolves links safely")
{
    TempVault temp;
    REQUIRE(fs::create_directory(temp.mPath / "A"));
    REQUIRE(fs::create_directory(temp.mPath / "B"));
    write(temp.mPath / "A" / "Source.md", "\xEF\xBB\xBF[[Target]] [[/B/Target]]");
    write(temp.mPath / "B" / "Target.md", "\xEF\xBB\xBFtarget");
    write(temp.mPath / "B" / "image.png", "image");
    std::string error;
    auto vault = Vault::open(utf8Path(temp.mPath), &error);
    REQUIRE(vault);
    if (!vault) return;
    REQUIRE(vault->resolveWikilink("Target", "A/Source.md", &error) == "B/Target.md");
    REQUIRE(vault->resolveWikilink("image.png", "A/Source.md", &error) == "B/image.png");
    REQUIRE(vault->attachmentPath("B/image.png") == fs::canonical(temp.mPath / "B/image.png"));
    REQUIRE(!vault->attachmentPath("B/Target.md"));
    error = "previous failure";
    REQUIRE(!vault->resolveWikilink("Missing", "A/Source.md", &error));
    REQUIRE(error.empty());
    error = "previous failure";
    REQUIRE(vault->resolveWikilink("Target", "A/Source.md", &error) == "B/Target.md");
    REQUIRE(error.empty());
    REQUIRE(!vault->resolveWikilink("../Target", "A/Source.md", &error));
    REQUIRE(error.empty());
    REQUIRE(vault->moveNote("B/Target.md", "A/Moved.md", "\xEF\xBB\xBFtarget", true, &error));
    REQUIRE(read(temp.mPath / "A" / "Source.md") == "\xEF\xBB\xBF[[/A/Moved]] [[/A/Moved]]");
    REQUIRE(read(temp.mPath / "A" / "Moved.md") == "\xEF\xBB\xBFtarget");
    REQUIRE(!vault->moveNote("A/Moved.md", "B/Target.md", "wrong", true, &error));
    REQUIRE(read(temp.mPath / "A" / "Moved.md") == "\xEF\xBB\xBFtarget");
    write(temp.mPath / "B" / "Twin.md", "twin");
    write(temp.mPath / "A" / "Twin.md", "near");
    REQUIRE(vault->resolveWikilink("Twin", "A/Source.md", &error) == "A/Twin.md");
    REQUIRE(!vault->resolveWikilink("/Missing", "A/Source.md", &error));
    write(temp.mPath / "A" / "Source.md", "[[Moved|alias]]");
    REQUIRE(!vault->moveNote("A/Moved.md", "B/Other.md", "\xEF\xBB\xBFtarget", true, &error));
    REQUIRE(fs::exists(temp.mPath / "A" / "Moved.md"));
    REQUIRE(!fs::exists(temp.mPath / "B" / "Other.md"));
    REQUIRE(vault->moveNote("A/Moved.md", "B/Other.md", "\xEF\xBB\xBFtarget", false, &error));
    REQUIRE(read(temp.mPath / "A" / "Source.md") == "[[Moved|alias]]");
}

TEST_CASE("Vault move rewrites Unicode casefolded links")
{
    TempVault temp;
    write(temp.mPath / nativePath("Caf\xC3\xA9.md"), "target");
    write(temp.mPath / "Source.md", "[[CAF\xC3\x89]]");
    std::string error;
    auto vault = Vault::open(utf8Path(temp.mPath), &error);
    REQUIRE(vault);
    if (!vault) return;
    REQUIRE(vault->resolveWikilink("CAF\xC3\x89", "Source.md", &error) == "Caf\xC3\xA9.md");
    REQUIRE(vault->moveNote("Caf\xC3\xA9.md", "Renamed.md", "target", true, &error));
    REQUIRE(read(temp.mPath / "Source.md") == "[[/Renamed]]");
}

TEST_CASE("Vault move preflight refuses unsafe links and paths")
{
    TempVault temp;
    TempVault outside;
    REQUIRE(fs::create_directory(temp.mPath / "Folder"));
    write(temp.mPath / "Target.md", "target");
    write(temp.mPath / "Links.md", "[[Target]]");
    std::string error;
    auto vault = Vault::open(utf8Path(temp.mPath), &error);
    REQUIRE(vault);
    if (!vault) return;
    const auto refused = [&](const std::string& to) {
        REQUIRE(!vault->moveNote("Target.md", to, "target", true, &error));
        REQUIRE(read(temp.mPath / "Target.md") == "target");
        REQUIRE(!fs::exists(temp.mPath / "Folder" / "Moved.md"));
    };
    refused("Missing/Moved.md");
    refused("Folder/../Moved.md");
    refused("Folder/Moved.md.md");
    write(temp.mPath / "Links.md", "]] [[Target]]");
    refused("Folder/Moved.md");
    write(temp.mPath / "Links.md", "[[Target]extra]]");
    refused("Folder/Moved.md");
    write(temp.mPath / "Links.md", "[[Unrelated\nlink]]");
    refused("Folder/Moved.md");
    write(temp.mPath / "Links.md", "[[Target|alias]]");
    refused("Folder/Moved.md");
    write(temp.mPath / "Links.md", "[[Target]]");
    write(temp.mPath / "Bad.md", "\xFF");
    refused("Folder/Moved.md");
    REQUIRE(fs::remove(temp.mPath / "Bad.md"));
    std::error_code ec;
    fs::create_directory_symlink(outside.mPath, temp.mPath / "Alias", ec);
    if (!ec) refused("Alias/Moved.md");
    write(temp.mPath / "Folder" / "Moved", "shadow");
    refused("Folder/Moved.md");
    REQUIRE(read(temp.mPath / "Folder" / "Moved") == "shadow");
}

TEST_CASE("Vault traversal errors are not mistaken for missing links")
{
#ifndef _WIN32
    TempVault temp;
    const auto blocked = temp.mPath / "Blocked";
    REQUIRE(fs::create_directory(blocked));
    write(temp.mPath / "Source.md", "[[Target]]");
    write(temp.mPath / "Target.md", "target");
    std::string error;
    auto vault = Vault::open(utf8Path(temp.mPath), &error);
    REQUIRE(vault);
    if (!vault) return;
    Session session;
    REQUIRE(session.openVault(utf8Path(temp.mPath), &error));
    REQUIRE(session.selectNote("Source.md", &error));
    fs::permissions(blocked, fs::perms::none);
    std::error_code probe;
    fs::directory_iterator iterator(blocked, probe);
    if (probe) {
        error.clear();
        REQUIRE(!vault->resolveWikilink("Target", "Source.md", &error));
        REQUIRE(error.find("Cannot discover vault files") != std::string::npos);
        error.clear();
        REQUIRE(!session.followWikilink("Target", &error));
        REQUIRE(error.find("Cannot discover vault files") != std::string::npos);
        error.clear();
        REQUIRE(!vault->moveNote("Target.md", "Moved.md", "target", true, &error));
        REQUIRE(error.find("Cannot discover vault files") != std::string::npos);
        REQUIRE(fs::exists(temp.mPath / "Target.md"));
        REQUIRE(!fs::exists(temp.mPath / "Moved.md"));
    }
    fs::permissions(blocked, fs::perms::owner_all);
#endif
}

TEST_CASE("Vault refuses trash when unavailable")
{
    TempVault temp;
    write(temp.mPath / "Note.md", "safe");
    std::string error;
    auto vault = Vault::open(utf8Path(temp.mPath), &error);
    REQUIRE(vault);
    if (!vault) return;
    REQUIRE(!vault->trashNote(
        "Note.md",
        [](const fs::path&, std::string* reason) {
            *reason = "Trash unavailable";
            return false;
        },
        &error));
    REQUIRE(error == "Trash unavailable");
    REQUIRE(read(temp.mPath / "Note.md") == "safe");
    REQUIRE(!vault->trashNote("../Note.md", &error));
}

#ifdef _WIN32
// Manual only: --test-case="Windows interactive trash smoke" --no-skip.
// Creates a unique disposable vault; never pass a personal vault to this test.
TEST_CASE("Windows interactive trash smoke" * doctest::skip())
{
    TempVault temp;
    const std::string note = temp.mPath.filename().string() + ".md";
    write(temp.mPath / note, "Disposable trash smoke test.\n");
    std::cout << "Windows shell delete for disposable file: " << temp.mPath / note << std::endl;
    std::string error;
    const auto vault = Vault::open(utf8Path(temp.mPath), &error);
    REQUIRE(vault);
    if (!vault) return;
    const bool deleted = vault->trashNote(note, &error);
    const bool remains = fs::exists(temp.mPath / note);
    std::cout << (deleted ? "Shell delete completed" : "Shell delete refused: " + error)
              << "; file remains: " << remains << '\n';
    REQUIRE(deleted != remains);
}
#endif

} // namespace
