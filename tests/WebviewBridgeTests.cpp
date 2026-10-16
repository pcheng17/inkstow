#include "Bridge.h"
#include "PortableTestSupport.h"

#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{
using Json = nlohmann::json;

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

TEST_CASE("Bridge handles vault operations, navigation, conflicts and shell failures")
{
    namespace fs = std::filesystem;
    inkstow::portable::test::TempVault vault;
    const fs::path& root = vault.mPath;
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
    std::string openedUrl;
    std::filesystem::path openedLink;
    inkstow::webview_shell::Bridge bridge(
        &shell,
        [&](const std::filesystem::path& path, std::string*) {
            openedLink = path;
            return true;
        },
        {},
        [&](const std::string& url, std::string*) {
            openedUrl = url;
            return true;
        });
    REQUIRE(
        request(
            bridge,
            {
                {"operation", "startup"}
    })["ok"] == true);
    REQUIRE(
        request(
            bridge,
            {
                {"operation", "pickVault"}
    })["ok"] == false);
    shell.mPicked = pathBytes(root);
    REQUIRE(
        request(
            bridge,
            {
                {"operation", "pickVault"}
    })["ok"] == true);
    REQUIRE(shell.mLast == pathBytes(root));
    REQUIRE(shell.mWatchingVault == pathBytes(root));
    REQUIRE(
        request(
            bridge,
            {
                {"operation", "startup"}
    })["notes"] == Json::array({"BOM.md", "Second.md"}));
    {
        std::ofstream added(root / "Added.md");
        added << "Added";
    }
    REQUIRE(
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
    REQUIRE((missingVault["ok"] == false && missingVault["notes"].empty()));
    REQUIRE(missingVault["error"].get<std::string>().find("missing") != std::string::npos);
    REQUIRE(shell.mLast == pathBytes(root / "missing"));
    shell.mPicked.clear();
    REQUIRE(
        request(
            reopened,
            {
                {"operation", "pickVault"}
    })["ok"] == false);
    REQUIRE(shell.mLast == pathBytes(root / "missing"));
    {
        std::ofstream(root / "Foo.bar.md") << "Dot note";
        std::ofstream(root / "image.png") << "image";
    }
    REQUIRE(
        request(
            bridge,
            {
                {"operation", "refresh"}
    })["notes"] ==
        Json::array({"BOM.md", "Foo.bar.md", "Second.md"}));
    REQUIRE(
        request(
            bridge,
            {
                {"operation",             "external"},
                {      "url", "https://example.org/"}
    })["ok"] == false);
    REQUIRE(openedUrl.empty());
    REQUIRE(
        request(
            bridge,
            {
                {"operation", "save"}
    })["ok"] == false);
    REQUIRE(
        request(
            bridge,
            {
                {"operation",          "open"},
                {   "folder", pathBytes(root)}
    })["ok"] == true);
    REQUIRE(
        request(
            bridge,
            {
                {"operation", "list"}
    })["notes"] ==
        Json::array({"BOM.md", "Foo.bar.md", "Second.md"}));
    REQUIRE(
        request(
            bridge,
            {
                {"operation", "select"},
                {     "path", "BOM.md"}
    })["content"] == "Original");
    REQUIRE(shell.mWatchingNote == "BOM.md");
    {
        std::ofstream attachment(root / "image.png");
        attachment << "image";
    }
    REQUIRE(
        request(
            bridge,
            {
                {"operation",    "follow"},
                {     "link", "image.png"}
    })["ok"] == true);
    const auto expectedAttachment = pathBytes(fs::canonical(root / "image.png"));
    REQUIRE(shell.mOpenedAttachment == expectedAttachment);
    REQUIRE(
        request(
            bridge,
            {
                {"operation",         "follow"},
                {     "link", "../outside.png"}
    })["ok"] == false);
    REQUIRE(
        request(
            bridge,
            {
                {"operation",    "link"},
                {     "link", "Foo.bar"}
    })["path"] == "Foo.bar.md");
    REQUIRE(
        request(
            bridge,
            {
                {"operation",           "link"},
                {     "link", "BOM.md#Section"}
    })["path"] == "BOM.md");
    REQUIRE(
        request(
            bridge,
            {
                {"operation",      "link"},
                {     "link", "image.png"}
    })["ok"] == true);
    REQUIRE(fs::equivalent(openedLink, root / "image.png"));
    REQUIRE(
        request(
            bridge,
            {
                {"operation",         "link"},
                {     "link", "../image.png"}
    })["ok"] == false);
    REQUIRE(fs::equivalent(openedLink, root / "image.png"));
    REQUIRE(
        request(
            bridge,
            {
                {"operation", "select"},
                {     "path", "BOM.md"}
    })["ok"] == true);
    REQUIRE(
        request(
            bridge,
            {
                {"operation",              "external"},
                {      "url", "https://example.org/a"}
    })["ok"] == true);
    REQUIRE(openedUrl == "https://example.org/a");
    REQUIRE(
        request(
            bridge,
            {
                {"operation",                   "external"},
                {      "url", "http://localhost:8080/path"}
    })["ok"] == true);
    REQUIRE(openedUrl == "http://localhost:8080/path");
    openedUrl = "https://example.org/a";
    for (const std::string url :
         {"file:///etc/passwd",
          "javascript:alert(1)",
          "https://example.org/\nInjected",
          "https://",
          "https://user@example.org",
          "https://:80/path",
          "https://example.org:/path",
          "https://example.org:abc/path",
          "https://example.org:65536/path",
          "https://example..org/path",
          "https://-bad.example/path",
          "https://example.org:80:90/path"})
    {
        REQUIRE(
            request(
                bridge,
                {
                    {"operation", "external"},
                    {      "url",        url}
        })["ok"] == false);
        REQUIRE(openedUrl == "https://example.org/a");
    }
    REQUIRE(
        request(
            bridge,
            {
                {"operation",    "edit"},
                {  "content", "Changed"}
    })["dirty"] == true);
    REQUIRE(
        request(
            bridge,
            {
                {"operation",    "follow"},
                {     "link", "image.png"}
    })["ok"] == false);
    REQUIRE(shell.mOpenedAttachment == expectedAttachment);
    REQUIRE(
        request(
            bridge,
            {
                {"operation",             "external"},
                {      "url", "https://example.org/"}
    })["ok"] == false);
    REQUIRE(openedUrl == "https://example.org/a");
    const Json refusedLink = request(
        bridge,
        {
            {"operation",    "link"},
            {     "link", "Foo.bar"}
    });
    REQUIRE(
        (refusedLink["ok"] == false && refusedLink["path"] == "BOM.md" &&
         refusedLink["content"] == "Changed" &&
         refusedLink["error"].get<std::string>().find("Save or discard") != std::string::npos));
    REQUIRE(
        request(
            bridge,
            {
                {"operation",      "link"},
                {     "link", "image.png"}
    })["ok"] == false);
    REQUIRE(fs::equivalent(openedLink, root / "image.png"));
    const Json refused = request(
        bridge,
        {
            {"operation",    "select"},
            {     "path", "Second.md"}
    });
    REQUIRE(
        (refused["ok"] == false && refused["path"] == "BOM.md" && refused["content"] == "Changed"));
    REQUIRE(
        request(
            bridge,
            {
                {"operation",          "open"},
                {   "folder", pathBytes(root)}
    })["ok"] == false);
    REQUIRE(
        request(
            bridge,
            {
                {"operation", "save"}
    })["dirty"] == false);
    REQUIRE(
        diskBytes(note) == "\xEF\xBB\xBF"
                           "Changed");
    REQUIRE(
        request(
            bridge,
            {
                {"operation",    "select"},
                {     "path", "Second.md"}
    })["ok"] == true);
    REQUIRE(
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
    REQUIRE(
        (conflict["ok"] == false && conflict["dirty"] == true && conflict["content"] == "Unsaved"));
    REQUIRE(diskBytes(root / "Second.md") == "Outside");
    REQUIRE(conflict["change"] == "conflict");
    const Json merged = request(
        bridge,
        {
            {"operation", "merge"}
    });
    REQUIRE((merged["ok"] == true && merged["dirty"] == true && merged["change"] == "none"));
    REQUIRE(merged["content"] == "<<<<<<< mine\nUnsaved\n=======\nOutside\n>>>>>>> outside");
    REQUIRE(diskBytes(root / "Second.md") == "Outside");
    REQUIRE(
        request(
            bridge,
            {
                {"operation", "save"}
    })["dirty"] == false);
    REQUIRE(
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
    REQUIRE(
        request(
            bridge,
            {
                {"operation", "refresh"}
    })["change"] == "conflict");
    REQUIRE(
        request(
            bridge,
            {
                {"operation", "keepMine"}
    })["dirty"] == false);
    REQUIRE(diskBytes(root / "Second.md") == "My next edit");
    REQUIRE(
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
    REQUIRE((missing["change"] == "missing" && missing["content"] == "Recover me"));
    REQUIRE(
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
    REQUIRE((refusedRecovery["ok"] == false && refusedRecovery["content"] == "Recover me"));
    REQUIRE(diskBytes(root / "Second.md") == "Reappeared");
    REQUIRE(
        request(
            bridge,
            {
                {"operation", "refresh"}
    })["change"] == "conflict");
    REQUIRE(
        request(
            bridge,
            {
                {"operation", "reload"}
    })["content"] == "Reappeared");
    REQUIRE(
        request(
            bridge,
            {
                {"operation",         "edit"},
                {  "content", "New recovery"}
    })["dirty"] == true);
    fs::remove(root / "Second.md");
    REQUIRE(
        request(
            bridge,
            {
                {"operation", "refresh"}
    })["change"] == "missing");
    REQUIRE(
        request(
            bridge,
            {
                {"operation", "saveAsNew"}
    })["dirty"] == false);
    REQUIRE(diskBytes(root / "Second.md") == "New recovery");
    REQUIRE(
        request(
            bridge,
            {
                {"operation",       "edit"},
                {  "content", "Discard me"}
    })["dirty"] == true);
    fs::remove(root / "Second.md");
    REQUIRE(
        request(
            bridge,
            {
                {"operation", "refresh"}
    })["change"] == "missing");
    REQUIRE(
        request(
            bridge,
            {
                {"operation", "discard"}
    })["dirty"] == false);
    REQUIRE(
        request(
            bridge,
            {
                {"operation", "discard"}
    })["ok"] == false);
    REQUIRE(
        request(
            bridge,
            {
                {"operation", "list"}
    })["change"] == "none");
    REQUIRE(
        request(
            bridge,
            {
                {"operation", "refresh"}
    })["change"] == "none");
    REQUIRE(
        request(
            bridge,
            {
                {"operation",          "edit"},
                {  "content", "Recover again"}
    })["dirty"] == true);
    REQUIRE(
        request(
            bridge,
            {
                {"operation", "refresh"}
    })["change"] == "missing");
    REQUIRE(
        request(
            bridge,
            Json{
                {"operation",    "select"},
                {     "path", "../BOM.md"}
    })["ok"] == false);
    REQUIRE(
        request(
            bridge,
            {
                {"operation",  "edit"},
                {  "content", "Draft"},
                {    "extra",       1}
    })["ok"] == false);
    REQUIRE(bridge.handle("not json").find("\"ok\":false") != std::string::npos);
    REQUIRE(bridge.handle("[{}]").find("\"ok\":false") != std::string::npos);
    REQUIRE(
        bridge.handle("[{\"operation\":\"edit\",\"content\":12}]").find("\"ok\":false") !=
        std::string::npos);
    REQUIRE(
        request(
            bridge,
            {
                {"operation", "list"}
    })["path"] == "Second.md");
    REQUIRE(
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
    REQUIRE(
        request(
            picker,
            {
                {"operation", "pickVault"}
    })["ok"] == true);
    REQUIRE(
        request(
            picker,
            {
                {"operation",  "select"},
                {     "path", "Safe.md"}
    })["ok"] == true);
    fs::remove(root / "Safe.md");
    REQUIRE(
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
    REQUIRE(
        (switchedMissing["ok"] == true && switchedMissing["change"] == "none" &&
         switchedMissing["path"].get<std::string>().empty() &&
         switchedMissing["notes"] == Json::array({"New.md"})));
    REQUIRE(
        request(
            picker,
            {
                {"operation", "reload"}
    })["ok"] == false);

    shell.mPicked = pathBytes(root);
    REQUIRE(
        request(
            picker,
            {
                {"operation", "pickVault"}
    })["ok"] == true);
    REQUIRE(
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
    REQUIRE(
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
    REQUIRE(
        (switchedConflict["ok"] == true && switchedConflict["change"] == "none" &&
         switchedConflict["path"].get<std::string>().empty() &&
         switchedConflict["notes"] == Json::array({"New.md"})));

    // A dismissed missing note must not suppress a new missing-note decision after switching.
    REQUIRE(
        request(
            picker,
            {
                {"operation", "select"},
                {     "path", "New.md"}
    })["ok"] == true);
    fs::remove(alternative / "New.md");
    REQUIRE(
        request(
            picker,
            {
                {"operation", "refresh"}
    })["change"] == "missing");
    REQUIRE(
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
    REQUIRE(
        request(
            picker,
            {
                {"operation", "pickVault"}
    })["ok"] == true);
    REQUIRE(
        request(
            picker,
            {
                {"operation",   "select"},
                {     "path", "Fresh.md"}
    })["ok"] == true);
    fs::remove(root / "Fresh.md");
    REQUIRE(
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
    REQUIRE(
        request(
            actions,
            {
                {"operation",          "open"},
                {   "folder", pathBytes(root)}
    })["ok"] == true);
    REQUIRE(
        request(
            actions,
            {
                {"operation", "create"},
                {     "path", "Target"}
    })["ok"] == false);
    REQUIRE(!fs::exists(root / "Target"));
    REQUIRE(
        request(
            actions,
            {
                {"operation",     "create"},
                {     "path", "Folder/New"}
    })["path"] == "Folder/New.md");
    REQUIRE(
        request(
            actions,
            {
                {  "operation",   "move"},
                {       "path", "Target"},
                {"updateLinks",     true}
    })["ok"] == false);
    REQUIRE(
        request(
            actions,
            {
                {"operation",    "select"},
                {     "path", "Target.md"}
    })["ok"] == true);
    REQUIRE(
        request(
            actions,
            {
                {  "operation",           "move"},
                {       "path", "Folder/Renamed"},
                {"updateLinks",             true}
    })["ok"] == true);
    REQUIRE(diskBytes(root / "Source.md") == "[[/Folder/Renamed]] [[image.png]]");
    REQUIRE(
        request(
            actions,
            {
                {"operation",    "select"},
                {     "path", "Source.md"}
    })["ok"] == true);
    REQUIRE(
        request(
            actions,
            {
                {"operation",          "follow"},
                {     "link", "/Folder/Renamed"}
    })["path"] == "Folder/Renamed.md");
    REQUIRE(
        request(
            actions,
            {
                {"operation",    "select"},
                {     "path", "Source.md"}
    })["ok"] == true);
    REQUIRE(
        request(
            actions,
            {
                {"operation", "attachment"},
                {     "link",  "image.png"}
    })["ok"] == true);
    // Vault canonicalizes attachments; temp paths can be aliases on macOS and Windows.
    REQUIRE((!opened.empty() && fs::equivalent(opened, root / "image.png")));
    inkstow::webview_shell::Bridge noShell;
    REQUIRE(
        request(
            noShell,
            {
                {"operation",          "open"},
                {   "folder", pathBytes(root)}
    })["ok"] == true);
    REQUIRE(
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
    REQUIRE(
        (shellFailure["ok"] == false &&
         shellFailure["error"] == "Attachment shell service is unavailable."));
    REQUIRE(
        request(
            actions,
            {
                {"operation",     "attachment"},
                {     "link", "../outside.png"}
    })["ok"] == false);
    REQUIRE(
        request(
            actions,
            {
                {"operation", "trash"}
    })["ok"] == false);
    REQUIRE(fs::exists(root / "Source.md"));
    REQUIRE(
        request(
            actions,
            {
                {"operation",  "edit"},
                {  "content", "draft"}
    })["dirty"] == true);
    REQUIRE(
        request(
            actions,
            {
                {"operation", "trash"}
    })["ok"] == false);
    REQUIRE(
        request(
            actions,
            {
                {"operation", "create"},
                {     "path",  "Draft"}
    })["ok"] == false);
    REQUIRE(
        request(
            actions,
            {
                {"operation", "attachment"},
                {     "link",  "image.png"}
    })["ok"] == false);
    REQUIRE((!opened.empty() && fs::equivalent(opened, root / "image.png")));
    REQUIRE(
        request(
            actions,
            {
                {"operation", "save"}
    })["ok"] == true);
    trashAvailable = true;
    // Injected trash service does not remove the file; the bridge must still call it.
    REQUIRE(
        request(
            actions,
            {
                {"operation", "trash"}
    })["ok"] == true);
    REQUIRE(request(
                actions,
                {
                    {"operation", "list"}
    })["path"]
                .get<std::string>()
                .empty());
}
