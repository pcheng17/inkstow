#include "GuardedReplace.h"
#include "Session.h"

#include <chrono>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace inkstow::portable;

#define CHECK(value)                                                                               \
    do {                                                                                           \
        if (!(value))                                                                              \
            throw std::runtime_error("Failed: " #value " at line " + std::to_string(__LINE__));    \
    } while (false)

namespace
{
void write(const fs::path &path, const std::string &bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    CHECK(out && out.write(bytes.data(), static_cast<std::streamsize>(bytes.size())));
}

std::string read(const fs::path &path)
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
    TempVault(const TempVault &) = delete;
    TempVault &operator=(const TempVault &) = delete;
    TempVault(TempVault &&) = delete;
    TempVault &operator=(TempVault &&) = delete;
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
    CHECK(session.openVault(temp.mPath.u8string(), &error));
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
    auto vault = Vault::open(temp.mPath.u8string(), &error);
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

void retryRechecksAndRefusals()
{
    TempVault temp;
    const fs::path path = temp.mPath / "Note.md";
    write(path, "old");
    std::string error;
    const auto valid = [](std::string *) { return true; };
    int tries = 0;
    const auto lockThenReplace =
        [&](const fs::path &stage, const fs::path &destination, std::string *reason) {
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
    const auto outsideWrite = [&](const fs::path &, const fs::path &, std::string *reason) {
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
    const auto alwaysLocked = [&](const fs::path &, const fs::path &, std::string *reason) {
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
    const auto stageChange = [&](std::string *) {
        if (++tries == 2) write(path, "outside");
        return true;
    };
    const bool stagedSaved =
        detail::guardedReplace(path, "old", "mine", stageChange, lockThenReplace, 3, &error);
    CHECK(stagedSaved == false);
    CHECK(read(path) == "outside");
    CHECK(tries == 2);
    for (const auto &entry : fs::directory_iterator(temp.mPath))
        CHECK(entry.path().filename() == "Note.md");
}
} // namespace

int main()
{
    try {
        fixtureDiscovery();
        editingAndEncoding();
        symlinkRefusal();
        retryRechecksAndRefusals();
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << "\n";
        return 1;
    }
}
