#include "Bridge.h"
#include "LocalHtml.h"
#include "NavigationPolicy.h"
#include "WebviewHeader.h"

#include <nlohmann/json.hpp>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <glib.h>
#endif

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void pumpFor(std::chrono::milliseconds duration)
{
    const auto end = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < end) {
#if defined(__APPLE__)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.001, true);
#elif defined(_WIN32)
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
#else
        while (g_main_context_pending(nullptr))
            g_main_context_iteration(nullptr, FALSE);
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

std::string pathBytes(const std::filesystem::path& path)
{
    const auto utf8 = path.u8string();
    return {utf8.begin(), utf8.end()};
}

std::string bytes(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void put(const std::filesystem::path& path, const std::string& content)
{
    std::ofstream file(path, std::ios::binary);
    file << content;
}
} // namespace

int run()
{
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / "inkstow-webview-conflict-window";
    fs::remove_all(root);
    fs::create_directories(root);
    const auto note = root / "Note.md";
    put(note, "Base");
    inkstow::webview_shell::Bridge bridge;
    webview::webview window(false, nullptr);
    inkstow::webview_shell::NavigationPolicy navigation(window.browser_controller().value());
    window.bind("inkstowRequest", [&bridge](const std::string& args) {
        return bridge.handle(args);
    });
    std::string observed;
    window.bind("probe", [&observed](const std::string& args) {
        observed = args;
        return std::string("null");
    });
    window.set_html(kLocalHtml);
    for (int attempt = 0; attempt < 100 && observed.find("ready") == std::string::npos; ++attempt) {
        window.eval("if (document.querySelector('#folder')) probe('ready')");
        pumpFor(std::chrono::milliseconds(100));
    }
    check(observed.find("ready") != std::string::npos, "Embedded editor did not load");
    const std::string folder = pathBytes(root);
    window.eval(
        "document.querySelector('#folder').value=" + nlohmann::json(folder).dump() +
        ";document.querySelector('#vault-form button').click();");
    pumpFor(std::chrono::milliseconds(350));
    window.eval("document.querySelector('#notes button').click()");
    pumpFor(std::chrono::milliseconds(350));
    window.eval(
        "document.querySelector('.cm-line').textContent='Mine';"
        "document.querySelector('.cm-content').dispatchEvent(new Event('input',{bubbles:true}));");
    pumpFor(std::chrono::milliseconds(350));
    put(note, "Outside");
    window.eval("window.dispatchEvent(new Event('focus'));");
    pumpFor(std::chrono::milliseconds(350));
    window.eval(
        "probe(JSON.stringify({conflict:!document.querySelector('#conflict').hidden,"
        "text:document.querySelector('.cm-content').textContent,"
        "error:document.querySelector('#error').textContent}));");
    pumpFor(std::chrono::milliseconds(150));
    check(
        observed.find("\\\"conflict\\\":true") != std::string::npos,
        "No conflict in real window");
    check(observed.find("Mine") != std::string::npos, "Outside edit lost local text");
    window.eval("document.querySelector('#save').click()");
    pumpFor(std::chrono::milliseconds(350));
    check(bytes(note) == "Outside", "Stale save replaced outside file");
    window.eval("document.querySelector('#merge').click()");
    pumpFor(std::chrono::milliseconds(350));
    window.eval(
        "probe(JSON.stringify({dirty:!document.querySelector('#dirty').hidden,"
        "text:document.querySelector('.cm-content').textContent}));");
    pumpFor(std::chrono::milliseconds(150));
    check(
        observed.find("<<<<<<< mine") != std::string::npos &&
            observed.find("Outside") != std::string::npos &&
            observed.find("\\\"dirty\\\":true") != std::string::npos,
        "Merge did not show editable unsaved markers");
    fs::remove(note);
    window.eval("window.dispatchEvent(new Event('focus'));");
    pumpFor(std::chrono::milliseconds(350));
    window.eval(
        "probe(JSON.stringify({missing:!document.querySelector('#missing').hidden,"
        "text:document.querySelector('.cm-content').textContent}));");
    pumpFor(std::chrono::milliseconds(150));
    check(
        observed.find("\\\"missing\\\":true") != std::string::npos &&
            observed.find("<<<<<<< mine") != std::string::npos,
        "Missing note lost merged edits");
    put(note, "Reappeared");
    window.eval("document.querySelector('#save-as-new').click()");
    pumpFor(std::chrono::milliseconds(350));
    check(bytes(note) == "Reappeared", "Save as new overwrote reappeared file");
    window.eval(
        "probe(JSON.stringify({error:document.querySelector('#error').textContent,"
        "text:document.querySelector('.cm-content').textContent}));");
    pumpFor(std::chrono::milliseconds(150));
    check(
        observed.find("<<<<<<< mine") != std::string::npos &&
            observed.find("error") != std::string::npos &&
            observed.find("exists") != std::string::npos,
        "Refused recovery did not explain error or retain editor text");
    fs::remove(note);
    window.eval("document.querySelector('#save-as-new').click()");
    pumpFor(std::chrono::milliseconds(350));
    check(
        bytes(note).find("<<<<<<< mine") != std::string::npos,
        "Recovery did not save unsaved text as a new note");
    window.eval("probe(JSON.stringify({dirty:!document.querySelector('#dirty').hidden}));");
    pumpFor(std::chrono::milliseconds(150));
    check(
        observed.find("\\\"dirty\\\":false") != std::string::npos,
        "Successful recovery left note dirty");
    fs::remove_all(root);
    return 0;
}

int main()
{
    try {
        return run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
