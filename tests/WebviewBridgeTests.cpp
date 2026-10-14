#include "Bridge.h"

#include <nlohmann/json.hpp>

#include <cassert>
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
    assert(
        request(
            bridge,
            {
                {"operation", "save"}
    })["ok"] == false);
    assert(
        request(
            bridge,
            {
                {"operation",          "open"},
                {   "folder", root.u8string()}
    })["ok"] == true);
    assert(
        request(
            bridge,
            {
                {"operation", "list"}
    })["notes"] == Json::array({"BOM.md", "Second.md"}));
    assert(
        request(
            bridge,
            {
                {"operation", "select"},
                {     "path", "BOM.md"}
    })["content"] == "Original");
    assert(
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
    assert(
        refused["ok"] == false && refused["path"] == "BOM.md" && refused["content"] == "Changed");
    assert(
        request(
            bridge,
            {
                {"operation",          "open"},
                {   "folder", root.u8string()}
    })["ok"] == false);
    assert(
        request(
            bridge,
            {
                {"operation", "save"}
    })["dirty"] == false);
    assert(
        diskBytes(note) == "\xEF\xBB\xBF"
                           "Changed");
    assert(
        request(
            bridge,
            {
                {"operation",    "select"},
                {     "path", "Second.md"}
    })["ok"] == true);
    assert(
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
    assert(
        conflict["ok"] == false && conflict["dirty"] == true && conflict["content"] == "Unsaved");
    assert(diskBytes(root / "Second.md") == "Outside");
    assert(
        request(
            bridge,
            Json{
                {"operation",    "select"},
                {     "path", "../BOM.md"}
    })["ok"] == false);
    assert(
        request(
            bridge,
            {
                {"operation",  "edit"},
                {  "content", "Draft"},
                {    "extra",       1}
    })["ok"] == false);
    assert(bridge.handle("not json").find("\"ok\":false") != std::string::npos);
    assert(bridge.handle("[{}]").find("\"ok\":false") != std::string::npos);
    assert(
        bridge.handle("[{\"operation\":\"edit\",\"content\":12}]").find("\"ok\":false") !=
        std::string::npos);
    assert(
        request(
            bridge,
            {
                {"operation", "list"}
    })["path"] == "Second.md");
    assert(
        request(
            bridge,
            Json{
                {"operation", "list"}
    })["content"] == "Unsaved");
    fs::remove_all(root);
}
