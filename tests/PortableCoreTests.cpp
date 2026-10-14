#include "GuardedReplace.h"
#include "Session.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <span>
#include <type_traits>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>

#include <cstdlib>
#endif

namespace fs = std::filesystem;
using namespace inkstow::portable;

#define CHECK(value)                                                                               \
    do {                                                                                           \
        if (!(value))                                                                              \
            throw std::runtime_error("Failed: " #value " at line " + std::to_string(__LINE__));    \
    } while (false)

namespace
{
std::string utf8Path(const fs::path& path)
{
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

void write(const fs::path& path, const std::string& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    CHECK(out && out.write(bytes.data(), static_cast<std::streamsize>(bytes.size())));
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
    TempVault() { CHECK(fs::create_directory(mPath)); }
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

void fixtureDiscovery()
{
    std::string error;
    auto vault = Vault::open(SAMPLE_VAULT, &error);
    CHECK(vault);
    std::vector<std::string> notes;
    CHECK(vault->listNotes(&notes, &error));
    CHECK((notes == std::vector<std::string>{"Folder/Second note.md", "Welcome.md"}));
    std::string bytes;
    CHECK(vault->readNote("Welcome.md", &bytes, &error));
    CHECK(!bytes.empty());
    CHECK(!vault->readNote("../Welcome.md", &bytes, &error));
    CHECK(!vault->readNote(".obsidian/Hidden.md", &bytes, &error));
    CHECK(!vault->readNote("Folder/../Welcome.md", &bytes, &error));
    CHECK(!vault->readNote("/Welcome.md", &bytes, &error));
}

void editingAndEncoding()
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
    CHECK(session.openVault(utf8Path(temp.mPath), &error));
    static_assert(std::is_same_v<decltype(session.getNotes()), std::span<const std::string>>);
    CHECK(session.getNotes().size() == 4);
    CHECK(session.getNotes()[1] == "Note.md");
    CHECK(!session.selectNote("Bad.md", &error));
    CHECK(!session.selectNote("Surrogate.md", &error));
    CHECK(!session.selectNote("Truncated.md", &error));
    CHECK(session.selectNote("Note.md", &error));
    CHECK(session.getContent() == "caf\xC3\xA9");
    session.setContent("edited \xE2\x98\x83");
    CHECK(session.isDirty());
    CHECK(!session.selectNote("Bad.md", &error));
    CHECK(session.save(&error));
    CHECK(!session.isDirty());
    CHECK(
        read(temp.mPath / "Note.md") == "\xEF\xBB\xBF"
                                        "edited \xE2\x98\x83");
    session.setContent("mine");
    write(temp.mPath / "Note.md", "outside");
    CHECK(!session.save(&error));
    CHECK(session.isDirty() && session.getContent() == "mine");
    CHECK(read(temp.mPath / "Note.md") == "outside");
    session.setContent("\xED\xA0\x80");
    CHECK(!session.save(&error));
    CHECK(session.isDirty());
}

void cleanOutsideChangeReloadsAndPreservesBom()
{
    TempVault temp;
    const fs::path note = temp.mPath / "Note.md";
    write(note, "original");
    Session session;
    std::string error;
    CHECK(session.syncWithDisk(&error) == DiskChange::None);
    CHECK(session.openVault(utf8Path(temp.mPath), &error));
    CHECK(session.selectNote("Note.md", &error));
    CHECK(session.syncWithDisk(&error) == DiskChange::None);
    write(
        note,
        "\xEF\xBB\xBF"
        "outside");
    CHECK(session.syncWithDisk(&error) == DiskChange::Reloaded);
    CHECK(session.getContent() == "outside");
    CHECK(!session.isDirty());
    session.setContent("saved");
    CHECK(session.save(&error));
    CHECK(
        read(note) == "\xEF\xBB\xBF"
                      "saved");
    CHECK(session.syncWithDisk(&error) == DiskChange::None);
}

void dirtyOutsideChangeRetainsEditsAndReportsSnapshot()
{
    TempVault temp;
    const fs::path note = temp.mPath / "Note.md";
    write(note, "original");
    Session session;
    std::string error;
    CHECK(session.openVault(utf8Path(temp.mPath), &error));
    CHECK(session.selectNote("Note.md", &error));
    session.setContent("mine");
    write(note, "outside");
    CHECK(session.syncWithDisk(&error) == DiskChange::Conflict);
    CHECK(session.getContent() == "mine" && session.isDirty());
    CHECK(session.overwriteDisk(&error));
    CHECK(read(note) == "mine");
    CHECK(!session.isDirty());
    session.setContent("mine again");
    write(note, "outside");
    CHECK(session.syncWithDisk(&error) == DiskChange::Conflict);
    CHECK(!session.save(&error));
    CHECK(read(note) == "outside");
    write(note, "\xFF");
    CHECK(session.syncWithDisk(&error) == DiskChange::Conflict);
    CHECK(session.getContent() == "mine again");
    CHECK(fs::remove(note));
    CHECK(session.syncWithDisk(&error) == DiskChange::Missing);
    CHECK(session.getPath() == "Note.md" && session.getContent() == "mine again");
}

void resolutionRequiresReportedOutsideVersion()
{
    TempVault temp;
    const fs::path note = temp.mPath / "Note.md";
    write(note, "original");
    Session session;
    std::string error;
    CHECK(session.openVault(utf8Path(temp.mPath), &error));
    CHECK(session.selectNote("Note.md", &error));
    session.setContent("mine");
    CHECK(!session.overwriteDisk(&error));
    write(note, "outside A");
    CHECK(session.syncWithDisk(&error) == DiskChange::Conflict);
    write(note, "outside B");
    CHECK(!session.overwriteDisk(&error));
    CHECK(read(note) == "outside B" && session.getContent() == "mine");
    CHECK(session.syncWithDisk(&error) == DiskChange::Conflict);
    CHECK(session.overwriteDisk(&error));
    CHECK(read(note) == "mine");
    session.setContent("new mine");
    write(note, "new outside");
    CHECK(session.syncWithDisk(&error) == DiskChange::Conflict);
    write(note, "\xFF");
    CHECK(!session.reloadFromDisk(&error));
    CHECK(error.find("UTF-8") != std::string::npos);
    CHECK(session.getContent() == "new mine" && session.isDirty());
    write(note, "new outside");
    CHECK(session.reloadFromDisk(&error)); // Explicit reload reads the latest disk version.
    CHECK(session.getContent() == "new outside");
    CHECK(!session.isDirty());
    CHECK(!session.overwriteDisk(&error));
    session.setContent("unsaved");
    CHECK(fs::remove(note));
    CHECK(!session.reloadFromDisk(&error));
    CHECK(!session.overwriteDisk(&error));
    CHECK(session.getContent() == "unsaved");
}

void mergeReportedOutsideChanges()
{
    TempVault temp;
    const fs::path note = temp.mPath / "Note.md";
    Session session;
    std::string error;
    write(note, "one\ntwo\nthree\nfour\n");
    CHECK(session.openVault(utf8Path(temp.mPath), &error));
    CHECK(session.selectNote("Note.md", &error));
    session.setContent("ONE\ntwo\nthree\nfour\n");
    write(note, "one\ntwo\nthree\nFOUR\n");
    CHECK(session.syncWithDisk(&error) == DiskChange::Conflict);
    CHECK(session.mergeOutsideChanges(&error));
    CHECK(session.getContent() == "ONE\ntwo\nthree\nFOUR\n" && session.isDirty());
    CHECK(read(note) == "one\ntwo\nthree\nFOUR\n");
    write(note, "newer\n");
    CHECK(!session.save(&error));
    CHECK(session.getContent() == "ONE\ntwo\nthree\nFOUR\n");
    write(note, "one\ntwo\nthree\nFOUR\n");
    CHECK(session.save(&error));
    CHECK(read(note) == "ONE\ntwo\nthree\nFOUR\n");

    write(note, "one\ntwo\nthree\n");
    CHECK(session.reloadFromDisk(&error));
    session.setContent("one\nlocal\nthree\n");
    write(note, "one\noutside\nthree\n");
    CHECK(session.syncWithDisk(&error) == DiskChange::Conflict);
    write(note, "one\nnewer\nthree\n");
    CHECK(!session.mergeOutsideChanges(&error));
    CHECK(session.getContent() == "one\nlocal\nthree\n");
    write(note, "one\noutside\nthree\n");
    CHECK(session.mergeOutsideChanges(&error));
    CHECK(
        session.getContent() ==
        "one\n<<<<<<< mine\nlocal\n=======\noutside\n>>>>>>> outside\nthree\n");
    CHECK(read(note) == "one\noutside\nthree\n");
    CHECK(session.save(&error));
    CHECK(read(note) == session.getContent());

    write(note, "one\ntwo\nthree\n");
    CHECK(session.reloadFromDisk(&error));
    session.setContent("one\nthree\n");
    write(note, "one\nchanged\nthree\n");
    CHECK(session.syncWithDisk(&error) == DiskChange::Conflict);
    CHECK(session.mergeOutsideChanges(&error));
    CHECK(session.getContent() == "one\n<<<<<<< mine\n=======\nchanged\n>>>>>>> outside\nthree\n");

    write(note, "a\nb\nc\nd\ne");
    CHECK(session.reloadFromDisk(&error));
    session.setContent("a\nX\ne");
    write(note, "a\nb\nc\nY\nd\ne");
    CHECK(session.syncWithDisk(&error) == DiskChange::Conflict);
    CHECK(session.mergeOutsideChanges(&error));
    CHECK(session.getContent() == "a\n<<<<<<< mine\nX\n=======\nb\nc\nY\nd\n>>>>>>> outside\ne");
    CHECK(read(note) == "a\nb\nc\nY\nd\ne");
}

void mergedEditorUsesReportedOutsideBom()
{
    TempVault temp;
    const fs::path note = temp.mPath / "Note.md";
    write(note, "first\nsecond\n");
    Session session;
    std::string error;
    CHECK(session.openVault(utf8Path(temp.mPath), &error));
    CHECK(session.selectNote("Note.md", &error));
    session.setContent("FIRST\nsecond\n");
    write(
        note,
        "\xEF\xBB\xBF"
        "first\nSECOND\n");
    CHECK(session.syncWithDisk(&error) == DiskChange::Conflict);
    CHECK(session.mergeOutsideChanges(&error));
    CHECK(session.getContent() == "FIRST\nSECOND\n");
    CHECK(session.save(&error));
    CHECK(
        read(note) == "\xEF\xBB\xBF"
                      "FIRST\nSECOND\n");
}

void missingNoteRecoveryRefusesReappearingPath()
{
    TempVault temp;
    CHECK(fs::create_directory(temp.mPath / "Folder"));
    const fs::path note = temp.mPath / "Folder" / "Note.md";
    write(
        note,
        "\xEF\xBB\xBF"
        "old");
    Session session;
    std::string error;
    CHECK(session.openVault(utf8Path(temp.mPath), &error));
    CHECK(session.selectNote("Folder/Note.md", &error));
    session.setContent("recovered");
    CHECK(fs::remove(note));
    CHECK(session.syncWithDisk(&error) == DiskChange::Missing);
    write(note, "reappeared");
    CHECK(!session.saveAsNewNote(&error));
    CHECK(read(note) == "reappeared" && session.getContent() == "recovered");
    CHECK(fs::remove(note));
    CHECK(session.saveAsNewNote(&error));
    CHECK(
        read(note) == "\xEF\xBB\xBF"
                      "recovered");
    CHECK(!session.isDirty());
    CHECK(session.syncWithDisk(&error) == DiskChange::None);
}

void discardEditsDoesNotRecreateDeletedNote()
{
    TempVault temp;
    const fs::path note = temp.mPath / "Note.md";
    write(note, "original");
    write(temp.mPath / "Other.md", "other");
    Session session;
    std::string error;
    CHECK(session.openVault(utf8Path(temp.mPath), &error));
    CHECK(session.selectNote("Note.md", &error));
    session.setContent("unsaved");
    CHECK(fs::remove(note));
    CHECK(session.syncWithDisk(&error) == DiskChange::Missing);
    session.discardEdits();
    CHECK(!session.isDirty());
    CHECK(session.getContent() == "original");
    CHECK(!fs::exists(note));
    CHECK(session.selectNote("Other.md", &error));
    CHECK(!fs::exists(note));
}

void mergeRefusesOversizedOrInvalidInputsWithoutLosingSnapshot()
{
    TempVault temp;
    const fs::path note = temp.mPath / "Note.md";
    std::string lines;
    for (int i = 0; i < 1500; ++i)
        lines += "x\n";
    write(note, lines);
    Session session;
    std::string error;
    CHECK(session.openVault(utf8Path(temp.mPath), &error));
    CHECK(session.selectNote("Note.md", &error));
    session.setContent(lines + "mine\n");
    write(note, lines + "outside\n");
    CHECK(session.syncWithDisk(&error) == DiskChange::Conflict);
    CHECK(!session.mergeOutsideChanges(&error));
    CHECK(error.find("too large") != std::string::npos);
    CHECK(session.getContent() == lines + "mine\n");
    CHECK(session.overwriteDisk(&error)); // Refused merge did not consume the reported version.
    CHECK(read(note) == lines + "mine\n");

    session.setContent("mine");
    write(note, "\xFF");
    CHECK(session.syncWithDisk(&error) == DiskChange::Conflict);
    CHECK(!session.mergeOutsideChanges(&error));
    CHECK(error.find("UTF-8") != std::string::npos);
    CHECK(session.getContent() == "mine" && read(note) == "\xFF");
    write(note, "outside");
    CHECK(session.syncWithDisk(&error) == DiskChange::Conflict);
    session.setContent("\xC0\xAF");
    CHECK(!session.mergeOutsideChanges(&error));
    CHECK(!session.overwriteDisk(&error));
    CHECK(!session.saveAsNewNote(&error));
    CHECK(read(note) == "outside");
}

void recoveryRespectsSelectedPathSafety()
{
    TempVault temp;
    TempVault other;
    const fs::path folder = temp.mPath / "Folder";
    CHECK(fs::create_directory(folder));
    const fs::path note = folder / "Note.md";
    write(note, "original");
    Session session;
    std::string error;
    CHECK(session.openVault(utf8Path(temp.mPath), &error));
    CHECK(session.selectNote("Folder/Note.md", &error));
    session.setContent("mine");
    CHECK(fs::remove(note));
    CHECK(fs::remove(folder));
    CHECK(!session.saveAsNewNote(&error));
    CHECK(session.getContent() == "mine" && session.isDirty());
    CHECK(!fs::exists(folder));
    std::error_code ec;
    fs::create_directory_symlink(other.mPath, folder, ec);
    if (!ec) {
        CHECK(!session.saveAsNewNote(&error));
        CHECK(!fs::exists(other.mPath / "Note.md"));
        CHECK(fs::remove(folder));
    }
    CHECK(fs::create_directory(folder));
    const fs::path target = other.mPath / "Target.md";
    write(target, "outside");
    fs::create_symlink(target, note, ec);
    if (!ec) {
        CHECK(!session.saveAsNewNote(&error));
        CHECK(!session.save(&error));
        CHECK(read(target) == "outside");
        CHECK(fs::remove(note));
    }
    CHECK(session.saveAsNewNote(&error));
    CHECK(read(note) == "mine");
}

void recoveryCreatesMissingNoteInExistingFolder()
{
    TempVault temp;
    CHECK(fs::create_directory(temp.mPath / "Folder"));
    std::string error;
    auto vault = Vault::open(utf8Path(temp.mPath), &error);
    CHECK(vault);
    const std::string bytes = "\xEF\xBB\xBF"
                              "Recovered\n";
    CHECK(vault->createNoteWithContent("Folder/Recovered.md", bytes, &error));
    CHECK(read(temp.mPath / "Folder" / "Recovered.md") == bytes);
    std::string opened;
    CHECK(vault->readNote("Folder/Recovered.md", &opened, &error));
    CHECK(opened == bytes);
}

void recoveryNeverOverwritesReappearedNote()
{
    TempVault temp;
    std::string error;
    auto vault = Vault::open(utf8Path(temp.mPath), &error);
    CHECK(vault);
    const fs::path destination = temp.mPath / "Recovered.md";
    write(destination, "outside");
    CHECK(!vault->createNoteWithContent("Recovered.md", "mine", &error));
    CHECK(error.find("already exists") != std::string::npos);
    CHECK(read(destination) == "outside");
    CHECK(fs::remove(destination));
    CHECK(vault->createNoteWithContent("Recovered.md", "mine", &error));
    CHECK(read(destination) == "mine");
    CHECK(!vault->createNoteWithContent("Recovered.md", "again", &error));
    CHECK(error.find("already exists") != std::string::npos);
    CHECK(read(destination) == "mine");
}

void recoveryRejectsInvalidPathsAndMissingParent()
{
    TempVault temp;
    std::string error;
    auto vault = Vault::open(utf8Path(temp.mPath), &error);
    CHECK(vault);
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
        CHECK(!vault->createNoteWithContent(path, "mine", &error));
        CHECK(error.find("visible relative Markdown path") != std::string::npos);
    }
    error.clear();
    CHECK(!vault->createNoteWithContent("Missing/Note.md", "mine", &error));
    CHECK(error.find("note parent folder") != std::string::npos);
    CHECK(!fs::exists(temp.mPath / "Missing"));
    write(temp.mPath / "File.md", "outside");
    CHECK(!vault->createNoteWithContent("File.md/Note.md", "mine", &error));
    CHECK(read(temp.mPath / "File.md") == "outside");
}

void recoveryRejectsSymlinkTargetsAndParents()
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
    CHECK(vault);
    CHECK(!vault->createNoteWithContent("Linked.md", "mine", &error));
    CHECK(error.find("already exists") != std::string::npos);
    if (!ec) {
        CHECK(!vault->createNoteWithContent("Alias/Note.md", "mine", &error));
        CHECK(!fs::exists(other.mPath / "Note.md"));
    }
    CHECK(read(outside) == "outside");
}

#ifdef _WIN32
void recoveryRejectsWindowsJunctionParent()
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
    CHECK(attributes != INVALID_FILE_ATTRIBUTES);
    CHECK((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0);
    CHECK((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
    CHECK(!fs::exists(outsideNote));
    std::string error;
    auto vault = Vault::open(utf8Path(temp.mPath), &error);
    CHECK(vault);
    CHECK(!vault->createNoteWithContent("Junction/Note.md", "mine", &error));
    CHECK(error.find("reparse point") != std::string::npos);
    CHECK(!fs::exists(outsideNote));
}
#endif

void symlinkRefusal()
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
    CHECK(vault);
    std::vector<std::string> notes;
    CHECK(vault->listNotes(&notes, &error));
    CHECK((notes == std::vector<std::string>{"Folder/Inside.md", "Good.md"}));
    std::string bytes;
    CHECK(!vault->readNote("Link.md", &bytes, &error));
    if (!ec) CHECK(!vault->readNote("Alias/Inside.md", &bytes, &error));
    CHECK(!vault->saveNote("Link.md", "good", "bad", &error));
    CHECK(read(temp.mPath / "Good.md") == "good");
}

void createAction()
{
    TempVault temp;
    std::string error;
    auto vault = Vault::open(utf8Path(temp.mPath), &error);
    CHECK(vault);
    CHECK(vault->createNote("Folder/Deep/New.md", &error));
    CHECK(read(temp.mPath / "Folder" / "Deep" / "New.md").empty());
    CHECK(!vault->createNote("Folder/Deep/New.md", &error));
    CHECK(!vault->createNote(".hidden/Note.md", &error));
    CHECK(!vault->createNote("Folder/../Escape.md", &error));
    CHECK(!fs::exists(temp.mPath / "Escape.md"));
    TempVault outside;
    std::error_code ec;
    fs::create_directory_symlink(outside.mPath, temp.mPath / "Alias", ec);
    if (!ec) {
        CHECK(!vault->createNote("Alias/Other.md", &error));
        CHECK(!fs::exists(outside.mPath / "Other.md"));
    }
}

void sessionUsesDiscoveredCaseOnCaseInsensitiveVolumes()
{
    TempVault temp;
    CHECK(fs::create_directory(temp.mPath / "Mixed"));
    std::error_code ec;
    if (!fs::equivalent(temp.mPath / "Mixed", temp.mPath / "mixed", ec) || ec)
        return; // Linux's usual case-sensitive volumes cannot exercise this behavior.
    Session session;
    std::string error;
    CHECK(session.openVault(utf8Path(temp.mPath), &error));
    CHECK(session.createNote("mixed/New.md", &error));
    CHECK(session.getPath() == "Mixed/New.md");
    CHECK(std::ranges::find(session.getNotes(), "Mixed/New.md") != session.getNotes().end());
    CHECK(session.moveNote("mixed/Renamed.md", false, &error));
    CHECK(session.getPath() == "Mixed/Renamed.md");
    CHECK(std::ranges::find(session.getNotes(), "Mixed/Renamed.md") != session.getNotes().end());
}

void linkActions()
{
    TempVault temp;
    CHECK(fs::create_directory(temp.mPath / "A"));
    CHECK(fs::create_directory(temp.mPath / "B"));
    write(temp.mPath / "A" / "Source.md", "\xEF\xBB\xBF[[Target]] [[/B/Target]]");
    write(temp.mPath / "B" / "Target.md", "\xEF\xBB\xBFtarget");
    write(temp.mPath / "B" / "image.png", "image");
    std::string error;
    auto vault = Vault::open(utf8Path(temp.mPath), &error);
    CHECK(vault);
    CHECK(vault->resolveWikilink("Target", "A/Source.md") == "B/Target.md");
    CHECK(vault->resolveWikilink("image.png", "A/Source.md") == "B/image.png");
    CHECK(vault->attachmentPath("B/image.png") == fs::canonical(temp.mPath / "B/image.png"));
    CHECK(!vault->attachmentPath("B/Target.md"));
    CHECK(!vault->resolveWikilink("Missing", "A/Source.md"));
    CHECK(!vault->resolveWikilink("../Target", "A/Source.md"));
    CHECK(vault->moveNote("B/Target.md", "A/Moved.md", "\xEF\xBB\xBFtarget", true, &error));
    CHECK(read(temp.mPath / "A" / "Source.md") == "\xEF\xBB\xBF[[/A/Moved]] [[/A/Moved]]");
    CHECK(read(temp.mPath / "A" / "Moved.md") == "\xEF\xBB\xBFtarget");
    CHECK(!vault->moveNote("A/Moved.md", "B/Target.md", "wrong", true, &error));
    CHECK(read(temp.mPath / "A" / "Moved.md") == "\xEF\xBB\xBFtarget");
    write(temp.mPath / "B" / "Twin.md", "twin");
    write(temp.mPath / "A" / "Twin.md", "near");
    CHECK(vault->resolveWikilink("Twin", "A/Source.md") == "A/Twin.md");
    CHECK(!vault->resolveWikilink("/Missing", "A/Source.md"));
    write(temp.mPath / "A" / "Source.md", "[[Moved|alias]]");
    CHECK(!vault->moveNote("A/Moved.md", "B/Other.md", "\xEF\xBB\xBFtarget", true, &error));
    CHECK(fs::exists(temp.mPath / "A" / "Moved.md"));
    CHECK(!fs::exists(temp.mPath / "B" / "Other.md"));
    CHECK(vault->moveNote("A/Moved.md", "B/Other.md", "\xEF\xBB\xBFtarget", false, &error));
    CHECK(read(temp.mPath / "A" / "Source.md") == "[[Moved|alias]]");
}

void sessionNoteActions()
{
    TempVault temp;
    write(temp.mPath / "Source.md", "[[Target]]");
    write(temp.mPath / "Target.md", "target");
    Session session;
    std::string error;
    CHECK(session.openVault(utf8Path(temp.mPath), &error));
    CHECK(session.selectNote("Target.md", &error));
    session.setContent("unsaved");
    CHECK(!session.moveNote("Moved.md", true, &error));
    CHECK(!session.trashNote(&error));
    CHECK(!session.createNote("New.md", &error));
    CHECK(read(temp.mPath / "Target.md") == "target");
    session.discardEdits();
    CHECK(session.moveNote("  Moved.md  ", true, &error));
    CHECK(session.getPath() == "Moved.md" && session.getContent() == "target");
    CHECK(read(temp.mPath / "Source.md") == "[[/Moved]]");
    CHECK(session.createNote("New.md", &error));
    CHECK(session.getPath() == "New.md" && session.getContent().empty());
    CHECK(!session.createNote("New.md", &error));
    CHECK(session.createNote("Upper.MD", &error));
    CHECK(session.getPath() == "Upper.MD");
    CHECK(session.createNote("  Trimmed  ", &error));
    CHECK(session.getPath() == "Trimmed.md");
    CHECK(fs::exists(temp.mPath / "Trimmed.md"));
    CHECK(session.createNote("Cafe\xCC\x81", &error));
    CHECK(session.getPath() == "Caf\xC3\xA9.md");
    CHECK(fs::exists(temp.mPath / "Caf\xC3\xA9.md"));
    CHECK(session.selectNote("Source.md", &error));
    CHECK(session.followWikilink("Moved", &error));
    CHECK(session.getPath() == "Moved.md");
    CHECK(!session.trashNote(
        [](const fs::path&, std::string* reason) {
            *reason = "Trash unavailable";
            return false;
        },
        &error));
    CHECK(session.getPath() == "Moved.md" && fs::exists(temp.mPath / "Moved.md"));
    TempVault trash;
    CHECK(session.trashNote(
        [&](const fs::path& path, std::string* reason) {
            std::error_code ec;
            fs::rename(path, trash.mPath / "Moved.md", ec);
            if (ec) *reason = ec.message();
            return !ec;
        },
        &error));
    CHECK(session.getPath().empty() && fs::exists(trash.mPath / "Moved.md"));
}

void movePreflightRefusesUnsafeLinksAndPaths()
{
    TempVault temp;
    TempVault outside;
    CHECK(fs::create_directory(temp.mPath / "Folder"));
    write(temp.mPath / "Target.md", "target");
    write(temp.mPath / "Links.md", "[[Target]]");
    std::string error;
    auto vault = Vault::open(utf8Path(temp.mPath), &error);
    CHECK(vault);
    const auto refused = [&](const std::string& to) {
        CHECK(!vault->moveNote("Target.md", to, "target", true, &error));
        CHECK(read(temp.mPath / "Target.md") == "target");
        CHECK(!fs::exists(temp.mPath / "Folder" / "Moved.md"));
    };
    refused("Missing/Moved.md");
    refused("Folder/../Moved.md");
    refused("Folder/Moved.md.md");
    write(temp.mPath / "Links.md", "]] [[Target]]");
    refused("Folder/Moved.md");
    write(temp.mPath / "Links.md", "[[Target]extra]]");
    refused("Folder/Moved.md");
    write(temp.mPath / "Links.md", "[[Target|alias]]");
    refused("Folder/Moved.md");
    write(temp.mPath / "Links.md", "[[Target]]");
    write(temp.mPath / "Bad.md", "\xFF");
    refused("Folder/Moved.md");
    CHECK(fs::remove(temp.mPath / "Bad.md"));
    std::error_code ec;
    fs::create_directory_symlink(outside.mPath, temp.mPath / "Alias", ec);
    if (!ec) refused("Alias/Moved.md");
    write(temp.mPath / "Folder" / "Moved", "shadow");
    refused("Folder/Moved.md");
    CHECK(read(temp.mPath / "Folder" / "Moved") == "shadow");
}

void trashRefusesWhenUnavailable()
{
    TempVault temp;
    write(temp.mPath / "Note.md", "safe");
    std::string error;
    auto vault = Vault::open(utf8Path(temp.mPath), &error);
    CHECK(vault);
    CHECK(!vault->trashNote(
        "Note.md",
        [](const fs::path&, std::string* reason) {
            *reason = "Trash unavailable";
            return false;
        },
        &error));
    CHECK(error == "Trash unavailable" && read(temp.mPath / "Note.md") == "safe");
    CHECK(!vault->trashNote("../Note.md", &error));
}

void retryRechecksAndRefusals()
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
    CHECK(detail::guardedReplace(path, "old", "new", valid, lockThenReplace, 3, &error));
    CHECK(tries == 2);
    CHECK(read(path) == "new");
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
    CHECK(outsideSaved == false);
    CHECK(tries == 1);
    CHECK(read(path) == "outside");
    write(path, "old");
    tries = 0;
    const auto alwaysLocked = [&](const fs::path&, const fs::path&, std::string* reason) {
        ++tries;
        *reason = "locked";
        return detail::ReplaceResult::Retryable;
    };
    const bool lockedSaved =
        detail::guardedReplace(path, "old", "mine", valid, alwaysLocked, 3, &error);
    CHECK(lockedSaved == false);
    CHECK(tries == 3);
    CHECK(read(path) == "old");
    tries = 0;
    const auto stageChange = [&](std::string*) {
        if (++tries == 2) write(path, "outside");
        return true;
    };
    const bool stagedSaved =
        detail::guardedReplace(path, "old", "mine", stageChange, lockThenReplace, 3, &error);
    CHECK(stagedSaved == false);
    CHECK(read(path) == "outside");
    CHECK(tries == 2);
    for (const auto& entry : fs::directory_iterator(temp.mPath))
        CHECK(entry.path().filename() == "Note.md");
}
} // namespace

int main()
{
    try {
        fixtureDiscovery();
        editingAndEncoding();
        cleanOutsideChangeReloadsAndPreservesBom();
        dirtyOutsideChangeRetainsEditsAndReportsSnapshot();
        resolutionRequiresReportedOutsideVersion();
        mergeReportedOutsideChanges();
        mergedEditorUsesReportedOutsideBom();
        missingNoteRecoveryRefusesReappearingPath();
        discardEditsDoesNotRecreateDeletedNote();
        mergeRefusesOversizedOrInvalidInputsWithoutLosingSnapshot();
        recoveryRespectsSelectedPathSafety();
        recoveryCreatesMissingNoteInExistingFolder();
        recoveryNeverOverwritesReappearedNote();
        recoveryRejectsInvalidPathsAndMissingParent();
        recoveryRejectsSymlinkTargetsAndParents();
#ifdef _WIN32
        recoveryRejectsWindowsJunctionParent();
#endif
        symlinkRefusal();
        createAction();
        linkActions();
        trashRefusesWhenUnavailable();
        sessionNoteActions();
        sessionUsesDiscoveredCaseOnCaseInsensitiveVolumes();
        movePreflightRefusesUnsafeLinksAndPaths();
        retryRechecksAndRefusals();
    } catch (const std::exception& failure) {
        std::cerr << failure.what() << "\n";
        return 1;
    }
}
