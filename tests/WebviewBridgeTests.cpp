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
    std::string openedUrl;
    {
        std::ofstream output(root / "Foo.bar.md", std::ios::binary);
        output << "Dot note";
    }
    {
        std::ofstream output(root / "image.png", std::ios::binary);
        output << "image";
    }
    std::filesystem::path openedLink;
    inkstow::webview_shell::Bridge bridge(
        [&](const std::filesystem::path& path, std::string*) {
            openedLink = path;
            return true;
        },
        {},
        [&](const std::string& url, std::string*) {
            openedUrl = url;
            return true;
        });
    CHECK(
        request(
            bridge,
            {
                {"operation",             "external"},
                {      "url", "https://example.org/"}
    })["ok"] == false);
    CHECK(openedUrl.empty());
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
    })["notes"] ==
        Json::array({"BOM.md", "Foo.bar.md", "Second.md"}));
    CHECK(
        request(
            bridge,
            {
                {"operation", "select"},
                {     "path", "BOM.md"}
    })["content"] == "Original");
    CHECK(
        request(
            bridge,
            {
                {"operation",    "link"},
                {     "link", "Foo.bar"}
    })["path"] == "Foo.bar.md");
    CHECK(
        request(
            bridge,
            {
                {"operation",           "link"},
                {     "link", "BOM.md#Section"}
    })["path"] == "BOM.md");
    CHECK(
        request(
            bridge,
            {
                {"operation",      "link"},
                {     "link", "image.png"}
    })["ok"] == true);
    CHECK(openedLink == root / "image.png");
    CHECK(
        request(
            bridge,
            {
                {"operation",         "link"},
                {     "link", "../image.png"}
    })["ok"] == false);
    CHECK(openedLink == root / "image.png");
    CHECK(
        request(
            bridge,
            {
                {"operation", "select"},
                {     "path", "BOM.md"}
    })["ok"] == true);
    CHECK(
        request(
            bridge,
            {
                {"operation",              "external"},
                {      "url", "https://example.org/a"}
    })["ok"] == true);
    CHECK(openedUrl == "https://example.org/a");
    CHECK(
        request(
            bridge,
            {
                {"operation",                   "external"},
                {      "url", "http://localhost:8080/path"}
    })["ok"] == true);
    CHECK(openedUrl == "http://localhost:8080/path");
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
        CHECK(
            request(
                bridge,
                {
                    {"operation", "external"},
                    {      "url",        url}
        })["ok"] == false);
        CHECK(openedUrl == "https://example.org/a");
    }
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
                {"operation",             "external"},
                {      "url", "https://example.org/"}
    })["ok"] == false);
    CHECK(openedUrl == "https://example.org/a");
    const Json refusedLink = request(
        bridge,
        {
            {"operation",    "link"},
            {     "link", "Foo.bar"}
    });
    CHECK(
        refusedLink["ok"] == false && refusedLink["path"] == "BOM.md" &&
        refusedLink["content"] == "Changed" &&
        refusedLink["error"].get<std::string>().find("Save or discard") != std::string::npos);
    CHECK(
        request(
            bridge,
            {
                {"operation",      "link"},
                {     "link", "image.png"}
    })["ok"] == false);
    CHECK(openedLink == root / "image.png");
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
