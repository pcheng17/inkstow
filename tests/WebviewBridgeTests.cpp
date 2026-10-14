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
    inkstow::webview_shell::Bridge bridge;
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
    CHECK(
        request(
            bridge,
            {
                {"operation",    "edit"},
                {  "content", "Changed"}
    })["dirty"] == true);
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
    })["content"] == "New recovery");
    fs::remove_all(root);
}
