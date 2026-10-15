#include "Bridge.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace
{
using Json = nlohmann::json;

void check(bool passed, const char* expression)
{
    if (!passed) {
        throw std::runtime_error(std::string("Bridge check failed: ") + expression);
    }
}
#define CHECK(expression) check((expression), #expression)

Json request(inkstow::webview_shell::Bridge& bridge, const Json& body)
{
    return Json::parse(bridge.handle(Json::array({body}).dump()));
}

std::string diskBytes(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string pathBytes(const std::filesystem::path& path)
{
    const auto utf8 = path.u8string();
    return {utf8.begin(), utf8.end()};
}

} // namespace

// Test failures are allowed to propagate as a nonzero process exit.
int main() // NOLINT(bugprone-exception-escape)
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "inkstow-webview-bridge-tests";
    fs::remove_all(root);
    fs::create_directories(root);
    const fs::path note = root / "BOM.md";
    {
        std::ofstream output(note, std::ios::binary);
        output << "\xEF\xBB\xBF"
                  "Original";
    }
    {
        std::ofstream output(root / "Second.md", std::ios::binary);
        output << "Second";
    }
    struct Shell : inkstow::webview_shell::ShellServices
    {
        std::string mLast;
        std::string mPicked;
        std::string mOpenedAttachment;
        std::string mWatchingVault;
        std::string mWatchingNote;
        void watchVault(const std::string& folder) override { mWatchingVault = folder; }
        void watchNote(const std::string& note) override { mWatchingNote = note; }
        bool openAttachment(const std::filesystem::path& path) override
        {
            mOpenedAttachment = pathBytes(path);
            return true;
        }
        std::string lastVault() override { return mLast; }
        void setLastVault(const std::string& path) override { mLast = path; }
        std::string pickVault() override { return mPicked; }
    } shell;
    inkstow::webview_shell::Bridge bridge(&shell);
    CHECK(
        request(
            bridge,
            {
                {"operation", "startup"}
    })["ok"] == true);
    CHECK(
        request(
            bridge,
            {
                {"operation", "pickVault"}
    })["ok"] == false);
    shell.mPicked = pathBytes(root);
    CHECK(
        request(
            bridge,
            {
                {"operation", "pickVault"}
    })["ok"] == true);
    CHECK(shell.mLast == pathBytes(root));
    CHECK(shell.mWatchingVault == pathBytes(root));
    CHECK(
        request(
            bridge,
            {
                {"operation", "startup"}
    })["notes"] == Json::array({"BOM.md", "Second.md"}));
    {
        std::ofstream added(root / "Added.md");
        added << "Added";
    }
    CHECK(
        request(
            bridge,
            {
                {"operation", "refresh"}
    })["notes"] ==
        Json::array({"Added.md", "BOM.md", "Second.md"}));
    fs::remove(root / "Added.md");
    shell.mLast = pathBytes(root / "missing");
    inkstow::webview_shell::Bridge reopened(&shell);
    const Json missingVault = request(
        reopened,
        {
            {"operation", "startup"}
    });
    CHECK(missingVault["ok"] == false && missingVault["notes"].empty());
    CHECK(missingVault["error"].get<std::string>().find("missing") != std::string::npos);
    CHECK(shell.mLast == pathBytes(root / "missing"));
    shell.mPicked.clear();
    CHECK(
        request(
            reopened,
            {
                {"operation", "pickVault"}
    })["ok"] == false);
    CHECK(shell.mLast == pathBytes(root / "missing"));
    CHECK(
        request(
            bridge,
            {
                {"operation", "save"}
    })["ok"] == false);
    CHECK(
        request(
            bridge,
            {
                {"operation",          "open"},
                {   "folder", pathBytes(root)}
    })["ok"] == true);
    CHECK(
        request(
            bridge,
            {
                {"operation", "list"}
    })["notes"] == Json::array({"BOM.md", "Second.md"}));
    CHECK(
        request(
            bridge,
            {
                {"operation", "select"},
                {     "path", "BOM.md"}
    })["content"] == "Original");
    CHECK(shell.mWatchingNote == "BOM.md");
    {
        std::ofstream attachment(root / "image.png");
        attachment << "image";
    }
    CHECK(
        request(
            bridge,
            {
                {"operation",    "follow"},
                {     "link", "image.png"}
    })["ok"] == true);
    CHECK(shell.mOpenedAttachment == pathBytes(root / "image.png"));
    CHECK(
        request(
            bridge,
            {
                {"operation",         "follow"},
                {     "link", "../outside.png"}
    })["ok"] == false);
    fs::remove(root / "image.png");
    CHECK(
        request(
            bridge,
            {
                {"operation",    "edit"},
                {  "content", "Changed"}
    })["dirty"] == true);
    CHECK(
        request(
            bridge,
            {
                {"operation",    "follow"},
                {     "link", "image.png"}
    })["ok"] == false);
    CHECK(shell.mOpenedAttachment == pathBytes(root / "image.png"));
    const Json refused = request(
        bridge,
        {
            {"operation",    "select"},
            {     "path", "Second.md"}
    });
    CHECK(refused["ok"] == false && refused["path"] == "BOM.md" && refused["content"] == "Changed");
    CHECK(
        request(
            bridge,
            {
                {"operation",          "open"},
                {   "folder", pathBytes(root)}
    })["ok"] == false);
    CHECK(
        request(
            bridge,
            {
                {"operation", "save"}
    })["dirty"] == false);
    CHECK(
        diskBytes(note) == "\xEF\xBB\xBF"
                           "Changed");
    CHECK(
        request(
            bridge,
            {
                {"operation",    "select"},
                {     "path", "Second.md"}
    })["ok"] == true);
    CHECK(
        request(
            bridge,
            Json{
                {"operation",    "edit"},
                {  "content", "Unsaved"}
    })["dirty"] == true);
    {
        std::ofstream output(root / "Second.md", std::ios::binary);
        output << "Outside";
    }
    const Json conflict = request(
        bridge,
        Json{
            {"operation", "save"}
    });
    CHECK(conflict["ok"] == false && conflict["dirty"] == true && conflict["content"] == "Unsaved");
    CHECK(diskBytes(root / "Second.md") == "Outside");
    CHECK(conflict["change"] == "conflict");
    const Json merged = request(
        bridge,
        {
            {"operation", "merge"}
    });
    CHECK(merged["ok"] == true && merged["dirty"] == true && merged["change"] == "none");
    CHECK(merged["content"] == "<<<<<<< mine\nUnsaved\n=======\nOutside\n>>>>>>> outside");
    CHECK(diskBytes(root / "Second.md") == "Outside");
    CHECK(
        request(
            bridge,
            {
                {"operation", "save"}
    })["dirty"] == false);
    CHECK(
        request(
            bridge,
            {
                {"operation",         "edit"},
                {  "content", "My next edit"}
    })["dirty"] == true);
    {
        std::ofstream output(root / "Second.md", std::ios::binary);
        output << "New outside";
    }
    CHECK(
        request(
            bridge,
            {
                {"operation", "refresh"}
    })["change"] == "conflict");
    CHECK(
        request(
            bridge,
            {
                {"operation", "keepMine"}
    })["dirty"] == false);
    CHECK(diskBytes(root / "Second.md") == "My next edit");
    CHECK(
        request(
            bridge,
            {
                {"operation",       "edit"},
                {  "content", "Recover me"}
    })["dirty"] == true);
    fs::remove(root / "Second.md");
    const Json missing = request(
        bridge,
        {
            {"operation", "refresh"}
    });
    CHECK(missing["change"] == "missing" && missing["content"] == "Recover me");
    CHECK(
        request(
            bridge,
            {
                {"operation", "save"}
    })["ok"] == false);
    {
        std::ofstream output(root / "Second.md", std::ios::binary);
        output << "Reappeared";
    }
    const Json refusedRecovery = request(
        bridge,
        {
            {"operation", "saveAsNew"}
    });
    CHECK(refusedRecovery["ok"] == false && refusedRecovery["content"] == "Recover me");
    CHECK(diskBytes(root / "Second.md") == "Reappeared");
    CHECK(
        request(
            bridge,
            {
                {"operation", "refresh"}
    })["change"] == "conflict");
    CHECK(
        request(
            bridge,
            {
                {"operation", "reload"}
    })["content"] == "Reappeared");
    CHECK(
        request(
            bridge,
            {
                {"operation",         "edit"},
                {  "content", "New recovery"}
    })["dirty"] == true);
    fs::remove(root / "Second.md");
    CHECK(
        request(
            bridge,
            {
                {"operation", "refresh"}
    })["change"] == "missing");
    CHECK(
        request(
            bridge,
            {
                {"operation", "saveAsNew"}
    })["dirty"] == false);
    CHECK(diskBytes(root / "Second.md") == "New recovery");
    CHECK(
        request(
            bridge,
            {
                {"operation",       "edit"},
                {  "content", "Discard me"}
    })["dirty"] == true);
    fs::remove(root / "Second.md");
    CHECK(
        request(
            bridge,
            {
                {"operation", "refresh"}
    })["change"] == "missing");
    CHECK(
        request(
            bridge,
            {
                {"operation", "discard"}
    })["dirty"] == false);
    CHECK(
        request(
            bridge,
            {
                {"operation", "discard"}
    })["ok"] == false);
    CHECK(
        request(
            bridge,
            {
                {"operation", "list"}
    })["change"] == "none");
    CHECK(
        request(
            bridge,
            {
                {"operation", "refresh"}
    })["change"] == "none");
    CHECK(
        request(
            bridge,
            {
                {"operation",          "edit"},
                {  "content", "Recover again"}
    })["dirty"] == true);
    CHECK(
        request(
            bridge,
            {
                {"operation", "refresh"}
    })["change"] == "missing");
    CHECK(
        request(
            bridge,
            Json{
                {"operation",    "select"},
                {     "path", "../BOM.md"}
    })["ok"] == false);
    CHECK(
        request(
            bridge,
            {
                {"operation",  "edit"},
                {  "content", "Draft"},
                {    "extra",       1}
    })["ok"] == false);
    CHECK(bridge.handle("not json").find("\"ok\":false") != std::string::npos);
    CHECK(bridge.handle("[{}]").find("\"ok\":false") != std::string::npos);
    CHECK(
        bridge.handle("[{\"operation\":\"edit\",\"content\":12}]").find("\"ok\":false") !=
        std::string::npos);
    CHECK(
        request(
            bridge,
            {
                {"operation", "list"}
    })["path"] == "Second.md");
    CHECK(
        request(
            bridge,
            Json{
                {"operation", "list"}
    })["content"] == "Recover again");
    const fs::path alternative = root / "Alternative";
    fs::create_directories(alternative);
    {
        std::ofstream output(alternative / "New.md");
        output << "New";
    }
    {
        std::ofstream output(root / "Safe.md");
        output << "Safe";
    }
    inkstow::webview_shell::Bridge picker(&shell);
    shell.mPicked = pathBytes(root);
    CHECK(
        request(
            picker,
            {
                {"operation", "pickVault"}
    })["ok"] == true);
    CHECK(
        request(
            picker,
            {
                {"operation",  "select"},
                {     "path", "Safe.md"}
    })["ok"] == true);
    fs::remove(root / "Safe.md");
    CHECK(
        request(
            picker,
            {
                {"operation", "refresh"}
    })["change"] == "missing");
    shell.mPicked = pathBytes(alternative);
    const Json switchedMissing = request(
        picker,
        {
            {"operation", "pickVault"}
    });
    CHECK(
        switchedMissing["ok"] == true && switchedMissing["change"] == "none" &&
        switchedMissing["path"].get<std::string>().empty() &&
        switchedMissing["notes"] == Json::array({"New.md"}));
    CHECK(
        request(
            picker,
            {
                {"operation", "reload"}
    })["ok"] == false);

    shell.mPicked = pathBytes(root);
    CHECK(
        request(
            picker,
            {
                {"operation", "pickVault"}
    })["ok"] == true);
    CHECK(
        request(
            picker,
            {
                {"operation", "select"},
                {     "path", "BOM.md"}
    })["ok"] == true);
    {
        std::ofstream output(root / "BOM.md", std::ios::binary);
        output << "\xFF";
    }
    CHECK(
        request(
            picker,
            {
                {"operation", "refresh"}
    })["change"] == "conflict");
    shell.mPicked = pathBytes(alternative);
    const Json switchedConflict = request(
        picker,
        {
            {"operation", "pickVault"}
    });
    CHECK(
        switchedConflict["ok"] == true && switchedConflict["change"] == "none" &&
        switchedConflict["path"].get<std::string>().empty() &&
        switchedConflict["notes"] == Json::array({"New.md"}));

    // A dismissed missing note must not suppress a new missing-note decision after switching.
    CHECK(
        request(
            picker,
            {
                {"operation", "select"},
                {     "path", "New.md"}
    })["ok"] == true);
    fs::remove(alternative / "New.md");
    CHECK(
        request(
            picker,
            {
                {"operation", "refresh"}
    })["change"] == "missing");
    CHECK(
        request(
            picker,
            {
                {"operation", "discard"}
    })["ok"] == true);
    {
        std::ofstream output(root / "Fresh.md");
        output << "Fresh";
    }
    shell.mPicked = pathBytes(root);
    CHECK(
        request(
            picker,
            {
                {"operation", "pickVault"}
    })["ok"] == true);
    CHECK(
        request(
            picker,
            {
                {"operation",   "select"},
                {     "path", "Fresh.md"}
    })["ok"] == true);
    fs::remove(root / "Fresh.md");
    CHECK(
        request(
            picker,
            {
                {"operation", "refresh"}
    })["change"] == "missing");
    fs::remove_all(root);
    fs::create_directories(root / "Folder");
    {
        std::ofstream(root / "Source.md") << "[[Target]] [[image.png]]";
        std::ofstream(root / "Target.md") << "Target";
        std::ofstream(root / "image.png") << "image";
    }
    fs::path opened;
    bool trashAvailable = false;
    inkstow::webview_shell::Bridge actions(
        [&](const fs::path& path, std::string*) {
            opened = path;
            return true;
        },
        [&](const fs::path&, std::string* error) {
            *error = "System trash unavailable.";
            return trashAvailable;
        });
    CHECK(
        request(
            actions,
            {
                {"operation",          "open"},
                {   "folder", pathBytes(root)}
    })["ok"] == true);
    CHECK(
        request(
            actions,
            {
                {"operation", "create"},
                {     "path", "Target"}
    })["ok"] == false);
    CHECK(!fs::exists(root / "Target"));
    CHECK(
        request(
            actions,
            {
                {"operation",     "create"},
                {     "path", "Folder/New"}
    })["path"] == "Folder/New.md");
    CHECK(
        request(
            actions,
            {
                {  "operation",   "move"},
                {       "path", "Target"},
                {"updateLinks",     true}
    })["ok"] == false);
    CHECK(
        request(
            actions,
            {
                {"operation",    "select"},
                {     "path", "Target.md"}
    })["ok"] == true);
    CHECK(
        request(
            actions,
            {
                {  "operation",           "move"},
                {       "path", "Folder/Renamed"},
                {"updateLinks",             true}
    })["ok"] == true);
    CHECK(diskBytes(root / "Source.md") == "[[/Folder/Renamed]] [[image.png]]");
    CHECK(
        request(
            actions,
            {
                {"operation",    "select"},
                {     "path", "Source.md"}
    })["ok"] == true);
    CHECK(
        request(
            actions,
            {
                {"operation",          "follow"},
                {     "link", "/Folder/Renamed"}
    })["path"] == "Folder/Renamed.md");
    CHECK(
        request(
            actions,
            {
                {"operation",    "select"},
                {     "path", "Source.md"}
    })["ok"] == true);
    CHECK(
        request(
            actions,
            {
                {"operation", "attachment"},
                {     "link",  "image.png"}
    })["ok"] == true);
    // Vault canonicalizes attachments; temp paths can be aliases on macOS and Windows.
    CHECK(!opened.empty() && fs::equivalent(opened, root / "image.png"));
    inkstow::webview_shell::Bridge noShell;
    CHECK(
        request(
            noShell,
            {
                {"operation",          "open"},
                {   "folder", pathBytes(root)}
    })["ok"] == true);
    CHECK(
        request(
            noShell,
            {
                {"operation",    "select"},
                {     "path", "Source.md"}
    })["ok"] == true);
    const Json shellFailure = request(
        noShell,
        {
            {"operation", "attachment"},
            {     "link",  "image.png"}
    });
    CHECK(
        shellFailure["ok"] == false &&
        shellFailure["error"] == "Attachment shell service is unavailable.");
    CHECK(
        request(
            actions,
            {
                {"operation",     "attachment"},
                {     "link", "../outside.png"}
    })["ok"] == false);
    CHECK(
        request(
            actions,
            {
                {"operation", "trash"}
    })["ok"] == false);
    CHECK(fs::exists(root / "Source.md"));
    CHECK(
        request(
            actions,
            {
                {"operation",  "edit"},
                {  "content", "draft"}
    })["dirty"] == true);
    CHECK(
        request(
            actions,
            {
                {"operation", "trash"}
    })["ok"] == false);
    CHECK(
        request(
            actions,
            {
                {"operation", "create"},
                {     "path",  "Draft"}
    })["ok"] == false);
    CHECK(
        request(
            actions,
            {
                {"operation", "attachment"},
                {     "link",  "image.png"}
    })["ok"] == false);
    CHECK(!opened.empty() && fs::equivalent(opened, root / "image.png"));
    CHECK(
        request(
            actions,
            {
                {"operation", "save"}
    })["ok"] == true);
    trashAvailable = true;
    // Injected trash service does not remove the file; the bridge must still call it.
    CHECK(
        request(
            actions,
            {
                {"operation", "trash"}
    })["ok"] == true);
    CHECK(request(
              actions,
              {
                  {"operation", "list"}
    })["path"]
              .get<std::string>()
              .empty());
    fs::remove_all(root);
}
