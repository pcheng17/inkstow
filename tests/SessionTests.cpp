#include "PortableTestSupport.h"
#include "Session.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace inkstow::portable
{
std::optional<std::string> normalizeNoteInput(const std::string& input);
} // namespace inkstow::portable

namespace fs = std::filesystem;
using namespace inkstow::portable;
using namespace inkstow::portable::test;

namespace
{
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

TEST_CASE("Session follows missing note links without creating unsafe targets")
{
    TempVault temp;
    REQUIRE(fs::create_directory(temp.mPath / "Folder"));
    write(temp.mPath / "Folder" / "Source.md", "[[New]] [[/Root]] [[image.png]]");
    write(temp.mPath / "Folder" / "image.png", "image");
    Session session;
    std::string error;
    std::optional<fs::path> attachment;
    REQUIRE(session.openVault(utf8Path(temp.mPath), &error));
    REQUIRE(session.selectNote("Folder/Source.md", &error));
    REQUIRE(session.followLink("New|alias", &attachment, &error));
    REQUIRE(session.getPath() == "Folder/New.md");
    REQUIRE(session.getContent().empty());
    REQUIRE(read(temp.mPath / "Folder" / "New.md").empty());
    REQUIRE(session.selectNote("Folder/Source.md", &error));
    REQUIRE(session.followLink("/Root#heading", &attachment, &error));
    REQUIRE(session.getPath() == "Root.md");
    REQUIRE(session.selectNote("Folder/Source.md", &error));
    REQUIRE(session.followLink("Sub/Deep.md", &attachment, &error));
    REQUIRE(session.getPath() == "Folder/Sub/Deep.md");
    REQUIRE(session.selectNote("Folder/Source.md", &error));
    REQUIRE(session.followLink("image.png", &attachment, &error));
    REQUIRE(attachment == fs::canonical(temp.mPath / "Folder" / "image.png"));
    REQUIRE(session.getPath() == "Folder/Source.md");
    for (const auto& link : {"missing.png", "../Escape", "/.hidden", "", "https://example.org"}) {
        REQUIRE_FALSE(session.followLink(link, &attachment, &error));
        REQUIRE(session.getPath() == "Folder/Source.md");
    }
    REQUIRE_FALSE(fs::exists(temp.mPath / "Escape.md"));
    session.setContent("unsaved");
    REQUIRE_FALSE(session.followLink("Another", &attachment, &error));
    REQUIRE_FALSE(fs::exists(temp.mPath / "Folder" / "Another.md"));
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

} // namespace
