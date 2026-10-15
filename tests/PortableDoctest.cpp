#include "GuardedReplace.h"
#include "Session.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <span>
#include <type_traits>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>

#include <cstdlib>
#endif

namespace inkstow::portable
{
std::optional<std::string> normalizeNoteInput(const std::string& input);
} // namespace inkstow::portable

namespace fs = std::filesystem;
using namespace inkstow::portable;

namespace
{
std::string utf8Path(const fs::path& path)
{
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

fs::path nativePath(const std::string& utf8)
{
    return fs::path(std::u8string(utf8.begin(), utf8.end()));
}

void write(const fs::path& path, const std::string& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    REQUIRE(out);
    REQUIRE(out.write(bytes.data(), static_cast<std::streamsize>(bytes.size())));
}

std::string read(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>{in}, {});
}

struct TempVault
{
    fs::path mPath = fs::temp_directory_path() /
                     ("inkstow-portable-" +
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TempVault() { REQUIRE(fs::create_directory(mPath)); }
    TempVault(const TempVault&) = delete;
    TempVault& operator=(const TempVault&) = delete;
    TempVault(TempVault&&) = delete;
    TempVault& operator=(TempVault&&) = delete;
    ~TempVault()
    {
        std::error_code ignored;
        fs::remove_all(mPath, ignored);
    }
};

TEST_CASE("Vault discovers visible fixture notes and refuses unsafe paths")
{
    std::string error;
    auto vault = Vault::open(SAMPLE_VAULT, &error);
    REQUIRE(vault);
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

TEST_CASE("Session preserves BOM and rejects invalid UTF-8 edits")
{
    TempVault temp;
    write(
        temp.mPath / "Note.md",
        "\xEF\xBB\xBF"
        "caf\xC3\xA9");
    write(temp.mPath / "Bad.md", "\xC0\xAF");
    write(temp.mPath / "Surrogate.md", "\xED\xA0\x80");
    write(temp.mPath / "Truncated.md", "\xE2\x98");
    Session session;
    std::string error;
    REQUIRE(session.openVault(utf8Path(temp.mPath), &error));
    static_assert(std::is_same_v<decltype(session.getNotes()), std::span<const std::string>>);
    REQUIRE(session.getNotes().size() == 4);
    REQUIRE(session.getNotes()[1] == "Note.md");
    REQUIRE(!session.selectNote("Bad.md", &error));
    REQUIRE(!session.selectNote("Surrogate.md", &error));
    REQUIRE(!session.selectNote("Truncated.md", &error));
    REQUIRE(session.selectNote("Note.md", &error));
    REQUIRE(session.getContent() == "caf\xC3\xA9");
    session.setContent("edited \xE2\x98\x83");
    REQUIRE(session.isDirty());
    REQUIRE(!session.selectNote("Bad.md", &error));
    REQUIRE(session.save(&error));
    REQUIRE(!session.isDirty());
    REQUIRE(
        read(temp.mPath / "Note.md") == "\xEF\xBB\xBF"
                                        "edited \xE2\x98\x83");
    session.setContent("mine");
    write(temp.mPath / "Note.md", "outside");
    REQUIRE(!session.save(&error));
    REQUIRE(session.isDirty());
    REQUIRE(session.getContent() == "mine");
    REQUIRE(read(temp.mPath / "Note.md") == "outside");
    session.setContent("\xED\xA0\x80");
    REQUIRE(!session.save(&error));
    REQUIRE(session.isDirty());
}

TEST_CASE("Session reloads clean outside changes without losing BOM")
{
    TempVault temp;
    const fs::path note = temp.mPath / "Note.md";
    write(note, "original");
    Session session;
    std::string error;
    REQUIRE(session.syncWithDisk(&error) == DiskChange::None);
    REQUIRE(session.openVault(utf8Path(temp.mPath), &error));
    REQUIRE(session.selectNote("Note.md", &error));
    REQUIRE(session.syncWithDisk(&error) == DiskChange::None);
    write(
        note,
        "\xEF\xBB\xBF"
        "outside");
    REQUIRE(session.syncWithDisk(&error) == DiskChange::Reloaded);
    REQUIRE(session.getContent() == "outside");
    REQUIRE(!session.isDirty());
    session.setContent("saved");
    REQUIRE(session.save(&error));
    REQUIRE(
        read(note) == "\xEF\xBB\xBF"
                      "saved");
    REQUIRE(session.syncWithDisk(&error) == DiskChange::None);
}

TEST_CASE("Session retains dirty edits on outside changes")
{
    TempVault temp;
    const fs::path note = temp.mPath / "Note.md";
    write(note, "original");
    Session session;
    std::string error;
    REQUIRE(session.openVault(utf8Path(temp.mPath), &error));
    REQUIRE(session.selectNote("Note.md", &error));
    session.setContent("mine");
    write(note, "outside");
    REQUIRE(session.syncWithDisk(&error) == DiskChange::Conflict);
    REQUIRE(session.getContent() == "mine");
    REQUIRE(session.isDirty());
    REQUIRE(session.overwriteDisk(&error));
    REQUIRE(read(note) == "mine");
    REQUIRE(!session.isDirty());
    session.setContent("mine again");
    write(note, "outside");
    REQUIRE(session.syncWithDisk(&error) == DiskChange::Conflict);
    REQUIRE(!session.save(&error));
    REQUIRE(read(note) == "outside");
    write(note, "\xFF");
    REQUIRE(session.syncWithDisk(&error) == DiskChange::Conflict);
    REQUIRE(session.getContent() == "mine again");
    REQUIRE(fs::remove(note));
    REQUIRE(session.syncWithDisk(&error) == DiskChange::Missing);
    REQUIRE(session.getPath() == "Note.md");
    REQUIRE(session.getContent() == "mine again");
}

TEST_CASE("Session conflict decisions require the reported outside version")
{
    TempVault temp;
    const fs::path note = temp.mPath / "Note.md";
    write(note, "original");
    Session session;
    std::string error;
    REQUIRE(session.openVault(utf8Path(temp.mPath), &error));
    REQUIRE(session.selectNote("Note.md", &error));
    session.setContent("mine");
    REQUIRE(!session.overwriteDisk(&error));
    write(note, "outside A");
    REQUIRE(session.syncWithDisk(&error) == DiskChange::Conflict);
    write(note, "outside B");
    REQUIRE(!session.overwriteDisk(&error));
    REQUIRE(read(note) == "outside B");
    REQUIRE(session.getContent() == "mine");
    REQUIRE(session.syncWithDisk(&error) == DiskChange::Conflict);
    REQUIRE(session.overwriteDisk(&error));
    REQUIRE(read(note) == "mine");
    session.setContent("new mine");
    write(note, "new outside");
    REQUIRE(session.syncWithDisk(&error) == DiskChange::Conflict);
    write(note, "\xFF");
    REQUIRE(!session.reloadFromDisk(&error));
    REQUIRE(error.find("UTF-8") != std::string::npos);
    REQUIRE(session.getContent() == "new mine");
    REQUIRE(session.isDirty());
    write(note, "new outside");
    REQUIRE(session.reloadFromDisk(&error)); // Explicit reload reads the latest disk version.
    REQUIRE(session.getContent() == "new outside");
    REQUIRE(!session.isDirty());
    REQUIRE(!session.overwriteDisk(&error));
    session.setContent("unsaved");
    REQUIRE(fs::remove(note));
    REQUIRE(!session.reloadFromDisk(&error));
    REQUIRE(!session.overwriteDisk(&error));
    REQUIRE(session.getContent() == "unsaved");
}

TEST_CASE("Session merges reported outside changes")
{
    TempVault temp;
    const fs::path note = temp.mPath / "Note.md";
    Session session;
    std::string error;
    write(note, "one\ntwo\nthree\nfour\n");
    REQUIRE(session.openVault(utf8Path(temp.mPath), &error));
    REQUIRE(session.selectNote("Note.md", &error));
    session.setContent("ONE\ntwo\nthree\nfour\n");
    write(note, "one\ntwo\nthree\nFOUR\n");
    REQUIRE(session.syncWithDisk(&error) == DiskChange::Conflict);
    REQUIRE(session.mergeOutsideChanges(&error));
    REQUIRE(session.getContent() == "ONE\ntwo\nthree\nFOUR\n");
    REQUIRE(session.isDirty());
    REQUIRE(read(note) == "one\ntwo\nthree\nFOUR\n");
    write(note, "newer\n");
    REQUIRE(!session.save(&error));
    REQUIRE(session.getContent() == "ONE\ntwo\nthree\nFOUR\n");
    write(note, "one\ntwo\nthree\nFOUR\n");
    REQUIRE(session.save(&error));
    REQUIRE(read(note) == "ONE\ntwo\nthree\nFOUR\n");

    write(note, "one\ntwo\nthree\n");
    REQUIRE(session.reloadFromDisk(&error));
    session.setContent("one\nlocal\nthree\n");
    write(note, "one\noutside\nthree\n");
    REQUIRE(session.syncWithDisk(&error) == DiskChange::Conflict);
    write(note, "one\nnewer\nthree\n");
    REQUIRE(!session.mergeOutsideChanges(&error));
    REQUIRE(session.getContent() == "one\nlocal\nthree\n");
    write(note, "one\noutside\nthree\n");
    REQUIRE(session.mergeOutsideChanges(&error));
    REQUIRE(
        session.getContent() ==
        "one\n<<<<<<< mine\nlocal\n=======\noutside\n>>>>>>> outside\nthree\n");
    REQUIRE(read(note) == "one\noutside\nthree\n");
    REQUIRE(session.save(&error));
    REQUIRE(read(note) == session.getContent());

    write(note, "one\ntwo\nthree\n");
    REQUIRE(session.reloadFromDisk(&error));
    session.setContent("one\nthree\n");
    write(note, "one\nchanged\nthree\n");
    REQUIRE(session.syncWithDisk(&error) == DiskChange::Conflict);
    REQUIRE(session.mergeOutsideChanges(&error));
    REQUIRE(
        session.getContent() == "one\n<<<<<<< mine\n=======\nchanged\n>>>>>>> outside\nthree\n");

    write(note, "a\nb\nc\nd\ne");
    REQUIRE(session.reloadFromDisk(&error));
    session.setContent("a\nX\ne");
    write(note, "a\nb\nc\nY\nd\ne");
    REQUIRE(session.syncWithDisk(&error) == DiskChange::Conflict);
    REQUIRE(session.mergeOutsideChanges(&error));
    REQUIRE(session.getContent() == "a\n<<<<<<< mine\nX\n=======\nb\nc\nY\nd\n>>>>>>> outside\ne");
    REQUIRE(read(note) == "a\nb\nc\nY\nd\ne");
}

TEST_CASE("Session merge preserves untouched line endings")
{
    TempVault temp;
    const fs::path note = temp.mPath / "Note.md";
    Session session;
    std::string error;
    for (const std::string& separator : {std::string("\r\n"), std::string("\r")}) {
        const auto text = [&separator](std::string start) {
            start += separator;
            start += "End";
            start += separator;
            return start;
        };
        write(note, text("Base"));
        REQUIRE(session.openVault(utf8Path(temp.mPath), &error));
        REQUIRE(session.selectNote("Note.md", &error));
        session.setContent(text("Mine"));
        write(note, text("Outside"));
        REQUIRE(session.syncWithDisk(&error) == DiskChange::Conflict);
        REQUIRE(session.mergeOutsideChanges(&error));
        const std::string merged = session.getContent();
        if (separator == "\r\n") {
            REQUIRE(
                merged ==
                "<<<<<<< mine\r\nMine\r\n=======\r\nOutside\r\n>>>>>>> outside\r\nEnd\r\n");
        } else {
            REQUIRE(merged == "<<<<<<< mine\rMine\rEnd\r=======\rOutside\rEnd\r>>>>>>> outside");
        }
        std::string reviewed = merged;
        reviewed.replace(reviewed.find("Mine"), 4, "Reviewed");
        session.setContent(reviewed);
        REQUIRE(session.save(&error));
        REQUIRE(read(note) == reviewed);
    }
}

TEST_CASE("Session merge uses the reported outside BOM")
{
    TempVault temp;
    const fs::path note = temp.mPath / "Note.md";
    write(note, "first\nsecond\n");
    Session session;
    std::string error;
    REQUIRE(session.openVault(utf8Path(temp.mPath), &error));
    REQUIRE(session.selectNote("Note.md", &error));
    session.setContent("FIRST\nsecond\n");
    write(
        note,
        "\xEF\xBB\xBF"
        "first\nSECOND\n");
    REQUIRE(session.syncWithDisk(&error) == DiskChange::Conflict);
    REQUIRE(session.mergeOutsideChanges(&error));
    REQUIRE(session.getContent() == "FIRST\nSECOND\n");
    REQUIRE(session.save(&error));
    REQUIRE(
        read(note) == "\xEF\xBB\xBF"
                      "FIRST\nSECOND\n");
}

TEST_CASE("Session recovery refuses a reappearing note")
{
    TempVault temp;
    REQUIRE(fs::create_directory(temp.mPath / "Folder"));
    const fs::path note = temp.mPath / "Folder" / "Note.md";
    write(
        note,
        "\xEF\xBB\xBF"
        "old");
    Session session;
    std::string error;
    REQUIRE(session.openVault(utf8Path(temp.mPath), &error));
    REQUIRE(session.selectNote("Folder/Note.md", &error));
    session.setContent("recovered");
    REQUIRE(fs::remove(note));
    REQUIRE(session.syncWithDisk(&error) == DiskChange::Missing);
    write(note, "reappeared");
    REQUIRE(!session.saveAsNewNote(&error));
    REQUIRE(read(note) == "reappeared");
    REQUIRE(session.getContent() == "recovered");
    REQUIRE(fs::remove(note));
    REQUIRE(session.saveAsNewNote(&error));
    REQUIRE(
        read(note) == "\xEF\xBB\xBF"
                      "recovered");
    REQUIRE(!session.isDirty());
    REQUIRE(session.syncWithDisk(&error) == DiskChange::None);
}

TEST_CASE("Session discard does not recreate a deleted note")
{
    TempVault temp;
    const fs::path note = temp.mPath / "Note.md";
    write(note, "original");
    write(temp.mPath / "Other.md", "other");
    Session session;
    std::string error;
    REQUIRE(session.openVault(utf8Path(temp.mPath), &error));
    REQUIRE(session.selectNote("Note.md", &error));
    session.setContent("unsaved");
    REQUIRE(fs::remove(note));
    REQUIRE(session.syncWithDisk(&error) == DiskChange::Missing);
    session.discardEdits();
    REQUIRE(!session.isDirty());
    REQUIRE(session.getContent() == "original");
    REQUIRE(!fs::exists(note));
    REQUIRE(session.selectNote("Other.md", &error));
    REQUIRE(!fs::exists(note));
}

TEST_CASE("Session merge refuses oversized or invalid input without losing snapshot")
{
    TempVault temp;
    const fs::path note = temp.mPath / "Note.md";
    std::string lines;
    for (int i = 0; i < 1500; ++i)
        lines += "x\n";
    write(note, lines);
    Session session;
    std::string error;
    REQUIRE(session.openVault(utf8Path(temp.mPath), &error));
    REQUIRE(session.selectNote("Note.md", &error));
    session.setContent(lines + "mine\n");
    write(note, lines + "outside\n");
    REQUIRE(session.syncWithDisk(&error) == DiskChange::Conflict);
    REQUIRE(!session.mergeOutsideChanges(&error));
    REQUIRE(error.find("too large") != std::string::npos);
    REQUIRE(session.getContent() == lines + "mine\n");
    REQUIRE(session.overwriteDisk(&error)); // Refused merge did not consume the reported version.
    REQUIRE(read(note) == lines + "mine\n");

    session.setContent("mine");
    write(note, "\xFF");
    REQUIRE(session.syncWithDisk(&error) == DiskChange::Conflict);
    REQUIRE(!session.mergeOutsideChanges(&error));
    REQUIRE(error.find("UTF-8") != std::string::npos);
    REQUIRE(session.getContent() == "mine");
    REQUIRE(read(note) == "\xFF");
    write(note, "outside");
    REQUIRE(session.syncWithDisk(&error) == DiskChange::Conflict);
    session.setContent("\xC0\xAF");
    REQUIRE(!session.mergeOutsideChanges(&error));
    REQUIRE(!session.overwriteDisk(&error));
    REQUIRE(!session.saveAsNewNote(&error));
    REQUIRE(read(note) == "outside");
}

TEST_CASE("Session recovery respects selected path safety")
{
    TempVault temp;
    TempVault other;
    const fs::path folder = temp.mPath / "Folder";
    REQUIRE(fs::create_directory(folder));
    const fs::path note = folder / "Note.md";
    write(note, "original");
    Session session;
    std::string error;
    REQUIRE(session.openVault(utf8Path(temp.mPath), &error));
    REQUIRE(session.selectNote("Folder/Note.md", &error));
    session.setContent("mine");
    REQUIRE(fs::remove(note));
    REQUIRE(fs::remove(folder));
    REQUIRE(!session.saveAsNewNote(&error));
    REQUIRE(session.getContent() == "mine");
    REQUIRE(session.isDirty());
    REQUIRE(!fs::exists(folder));
    std::error_code ec;
    fs::create_directory_symlink(other.mPath, folder, ec);
    if (!ec) {
        REQUIRE(!session.saveAsNewNote(&error));
        REQUIRE(!fs::exists(other.mPath / "Note.md"));
        REQUIRE(fs::remove(folder));
    }
    REQUIRE(fs::create_directory(folder));
    const fs::path target = other.mPath / "Target.md";
    write(target, "outside");
    fs::create_symlink(target, note, ec);
    if (!ec) {
        REQUIRE(!session.saveAsNewNote(&error));
        REQUIRE(!session.save(&error));
        REQUIRE(read(target) == "outside");
        REQUIRE(fs::remove(note));
    }
    REQUIRE(session.saveAsNewNote(&error));
    REQUIRE(read(note) == "mine");
}

TEST_CASE("Session recovery creates missing notes only in existing folders")
{
    TempVault temp;
    REQUIRE(fs::create_directory(temp.mPath / "Folder"));
    std::string error;
    auto vault = Vault::open(utf8Path(temp.mPath), &error);
    REQUIRE(vault);
    const std::string bytes = "\xEF\xBB\xBF"
                              "Recovered\n";
    REQUIRE(vault->createNoteWithContent("Folder/Recovered.md", bytes, &error));
    REQUIRE(read(temp.mPath / "Folder" / "Recovered.md") == bytes);
    std::string opened;
    REQUIRE(vault->readNote("Folder/Recovered.md", &opened, &error));
    REQUIRE(opened == bytes);
}

TEST_CASE("Session recovery never overwrites a reappeared note")
{
    TempVault temp;
    std::string error;
    auto vault = Vault::open(utf8Path(temp.mPath), &error);
    REQUIRE(vault);
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

TEST_CASE("Session recovery rejects invalid paths and missing parents")
{
    TempVault temp;
    std::string error;
    auto vault = Vault::open(utf8Path(temp.mPath), &error);
    REQUIRE(vault);
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

TEST_CASE("Session recovery rejects symlink targets and parents")
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
    REQUIRE(!vault->createNoteWithContent("Linked.md", "mine", &error));
    REQUIRE(error.find("already exists") != std::string::npos);
    if (!ec) {
        REQUIRE(!vault->createNoteWithContent("Alias/Note.md", "mine", &error));
        REQUIRE(!fs::exists(other.mPath / "Note.md"));
    }
    REQUIRE(read(outside) == "outside");
}

#ifdef _WIN32
TEST_CASE("Session recovery rejects Windows junction parents")
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

TEST_CASE("Session move selects destination rather than hard link")
{
    TempVault temp;
    REQUIRE(fs::create_directory(temp.mPath / "A"));
    REQUIRE(fs::create_directory(temp.mPath / "B"));
    REQUIRE(fs::create_directory(temp.mPath / "C"));
    write(temp.mPath / "B" / "Target.md", "target");
    std::error_code ec;
    fs::create_hard_link(temp.mPath / "B" / "Target.md", temp.mPath / "A" / "Other.md", ec);
    if (ec) return; // Some filesystems do not support hard links.
    Session session;
    std::string error;
    REQUIRE(session.openVault(utf8Path(temp.mPath), &error));
    REQUIRE(session.selectNote("B/Target.md", &error));
    REQUIRE(session.moveNote("C/Target.md", false, &error));
    REQUIRE(session.getPath() == "C/Target.md");
    REQUIRE(std::ranges::find(session.getNotes(), "C/Target.md") != session.getNotes().end());
    session.setContent("edited");
    REQUIRE(session.save(&error));
    REQUIRE(read(temp.mPath / "C" / "Target.md") == "edited");
    REQUIRE(read(temp.mPath / "A" / "Other.md") == "target");
}

TEST_CASE("Session follows attachment links safely")
{
    TempVault temp;
    TempVault outside;
    REQUIRE(fs::create_directory(temp.mPath / "Folder"));
    write(temp.mPath / "Folder" / "Source.md", "[[image.png]]");
    write(temp.mPath / "Folder" / "image.png", "nearby");
    write(temp.mPath / "image.png", "root");
    Session session;
    std::string error;
    REQUIRE(session.openVault(utf8Path(temp.mPath), &error));
    REQUIRE(session.selectNote("Folder/Source.md", &error));
    REQUIRE(
        session.followAttachment("image.png", &error) ==
        fs::canonical(temp.mPath / "Folder" / "image.png"));
    REQUIRE(
        session.followAttachment("/image.png", &error) == fs::canonical(temp.mPath / "image.png"));
    REQUIRE(!session.followAttachment("missing.png", &error));
    REQUIRE(!session.followAttachment("../image.png", &error));
    REQUIRE(!session.followAttachment("Source", &error));
    session.setContent("unsaved");
    REQUIRE(!session.followAttachment("image.png", &error));
    REQUIRE(error.find("Save or discard") != std::string::npos);
    REQUIRE(session.getPath() == "Folder/Source.md");
    REQUIRE(session.getContent() == "unsaved");
    session.discardEdits();
    write(outside.mPath / "outside.png", "outside");
    std::error_code ec;
    fs::create_symlink(outside.mPath / "outside.png", temp.mPath / "Folder" / "link.png", ec);
    if (!ec) REQUIRE(!session.followAttachment("link.png", &error));
}

TEST_CASE("Session uses discovered case on case insensitive volumes")
{
    TempVault temp;
    REQUIRE(fs::create_directory(temp.mPath / "Mixed"));
    std::error_code ec;
    if (!fs::equivalent(temp.mPath / "Mixed", temp.mPath / "mixed", ec) || ec)
        return; // Linux's usual case-sensitive volumes cannot exercise this behavior.
    Session session;
    std::string error;
    REQUIRE(session.openVault(utf8Path(temp.mPath), &error));
    REQUIRE(session.createNote("mixed/New.md", &error));
    REQUIRE(session.getPath() == "Mixed/New.md");
    REQUIRE(std::ranges::find(session.getNotes(), "Mixed/New.md") != session.getNotes().end());
    REQUIRE(session.moveNote("mixed/Renamed.md", false, &error));
    REQUIRE(session.getPath() == "Mixed/Renamed.md");
    REQUIRE(std::ranges::find(session.getNotes(), "Mixed/Renamed.md") != session.getNotes().end());
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

TEST_CASE("Session guards create move and trash actions")
{
    TempVault temp;
    write(temp.mPath / "Source.md", "[[Target]]");
    write(temp.mPath / "Target.md", "target");
    Session session;
    std::string error;
    REQUIRE(session.openVault(utf8Path(temp.mPath), &error));
    REQUIRE(session.selectNote("Target.md", &error));
    session.setContent("unsaved");
    REQUIRE(!session.moveNote("Moved.md", true, &error));
    REQUIRE(!session.trashNote(&error));
    REQUIRE(!session.createNote("New.md", &error));
    REQUIRE(read(temp.mPath / "Target.md") == "target");
    session.discardEdits();
    const auto normalized = normalizeNoteInput("  Moved.md  ");
    if (!normalized || *normalized != "Moved.md")
        throw std::runtime_error(
            "Unexpected normalized path length: " +
            std::to_string(normalized ? normalized->size() : 0));
    if (!session.moveNote("  Moved.md  ", true, &error))
        throw std::runtime_error("Whitespace-trimmed move failed: " + error);
    REQUIRE(session.getPath() == "Moved.md");
    REQUIRE(session.getContent() == "target");
    REQUIRE(read(temp.mPath / "Source.md") == "[[/Moved]]");
    REQUIRE(session.createNote("New.md", &error));
    REQUIRE(session.getPath() == "New.md");
    REQUIRE(session.getContent().empty());
    REQUIRE(!session.createNote("New.md", &error));
    REQUIRE(session.createNote("Upper.MD", &error));
    REQUIRE(session.getPath() == "Upper.MD");
    REQUIRE(session.createNote("  Trimmed  ", &error));
    REQUIRE(session.getPath() == "Trimmed.md");
    REQUIRE(fs::exists(temp.mPath / "Trimmed.md"));
    REQUIRE(session.createNote("Cafe\xCC\x81", &error));
    REQUIRE(session.getPath() == "Caf\xC3\xA9.md");
    REQUIRE(fs::exists(temp.mPath / nativePath("Caf\xC3\xA9.md")));
    REQUIRE(session.moveNote("  Moved cafe\xCC\x81  ", false, &error));
    REQUIRE(session.getPath() == "Moved caf\xC3\xA9.md");
    REQUIRE(fs::exists(temp.mPath / nativePath("Moved caf\xC3\xA9.md")));
    REQUIRE(session.selectNote("Source.md", &error));
    REQUIRE(session.followWikilink("Moved", &error));
    REQUIRE(session.getPath() == "Moved.md");
    REQUIRE(!session.trashNote(
        [](const fs::path&, std::string* reason) {
            *reason = "Trash unavailable";
            return false;
        },
        &error));
    REQUIRE(session.getPath() == "Moved.md");
    REQUIRE(fs::exists(temp.mPath / "Moved.md"));
    TempVault trash;
    REQUIRE(session.trashNote(
        [&](const fs::path& path, std::string* reason) {
            std::error_code ec;
            fs::rename(path, trash.mPath / "Moved.md", ec);
            if (ec) *reason = ec.message();
            return !ec;
        },
        &error));
    REQUIRE(session.getPath().empty());
    REQUIRE(fs::exists(trash.mPath / "Moved.md"));
}

TEST_CASE("Vault move rewrites Unicode casefolded links")
{
    TempVault temp;
    write(temp.mPath / nativePath("Caf\xC3\xA9.md"), "target");
    write(temp.mPath / "Source.md", "[[CAF\xC3\x89]]");
    std::string error;
    auto vault = Vault::open(utf8Path(temp.mPath), &error);
    REQUIRE(vault);
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

TEST_CASE("Guarded replacement rechecks retries and refusals")
{
    TempVault temp;
    const fs::path path = temp.mPath / "Note.md";
    write(path, "old");
    std::string error;
    const auto valid = [](std::string*) { return true; };
    int tries = 0;
    const auto lockThenReplace =
        [&](const fs::path& stage, const fs::path& destination, std::string* reason) {
            if (++tries == 1) {
                *reason = "locked";
                return detail::ReplaceResult::Retryable;
            }
            return detail::replaceFile(stage, destination, reason);
        };
    REQUIRE(detail::guardedReplace(path, "old", "new", valid, lockThenReplace, 3, &error));
    REQUIRE(tries == 2);
    REQUIRE(read(path) == "new");
    tries = 0;
    write(path, "old");
    const auto outsideWrite = [&](const fs::path&, const fs::path&, std::string* reason) {
        ++tries;
        write(path, "outside");
        *reason = "locked";
        return detail::ReplaceResult::Retryable;
    };
    const bool outsideSaved =
        detail::guardedReplace(path, "old", "mine", valid, outsideWrite, 3, &error);
    REQUIRE(outsideSaved == false);
    REQUIRE(tries == 1);
    REQUIRE(read(path) == "outside");
    write(path, "old");
    tries = 0;
    const auto alwaysLocked = [&](const fs::path&, const fs::path&, std::string* reason) {
        ++tries;
        *reason = "locked";
        return detail::ReplaceResult::Retryable;
    };
    const bool lockedSaved =
        detail::guardedReplace(path, "old", "mine", valid, alwaysLocked, 3, &error);
    REQUIRE(lockedSaved == false);
    REQUIRE(tries == 3);
    REQUIRE(read(path) == "old");
    tries = 0;
    const auto stageChange = [&](std::string*) {
        if (++tries == 2) write(path, "outside");
        return true;
    };
    const bool stagedSaved =
        detail::guardedReplace(path, "old", "mine", stageChange, lockThenReplace, 3, &error);
    REQUIRE(stagedSaved == false);
    REQUIRE(read(path) == "outside");
    REQUIRE(tries == 2);
    for (const auto& entry : fs::directory_iterator(temp.mPath))
        REQUIRE(entry.path().filename() == "Note.md");
}
} // namespace
