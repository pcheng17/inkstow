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
#include <random>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
struct TempVault
{
    std::filesystem::path mPath;
    TempVault()
    {
        std::mt19937_64 generator(std::random_device{}());
        for (int attempt = 0; attempt < 32; ++attempt) {
            const auto candidate = std::filesystem::temp_directory_path() /
                                   ("inkstow-webview-conflict-" + std::to_string(generator()));
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                mPath = candidate;
                return;
            }
            if (error && error != std::errc::file_exists) {
                throw std::filesystem::filesystem_error(
                    "Could not create test vault",
                    candidate,
                    error);
            }
        }
        throw std::runtime_error("Could not reserve unique test vault");
    }
    ~TempVault()
    {
        if (mPath.empty()) return;
        std::error_code ignored;
        std::filesystem::remove_all(mPath, ignored);
    }
    TempVault(const TempVault&) = delete;
    TempVault& operator=(const TempVault&) = delete;
    TempVault(TempVault&&) = delete;
    TempVault& operator=(TempVault&&) = delete;
};

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

void replaceEditor(webview::webview& window, const std::string& text)
{
    // Browser editing commands follow CodeMirror's real input path (selection, beforeinput, input).
    window.eval(
        "{const editor=document.querySelector('#editor [contenteditable=true]');"
        "editor.focus();document.execCommand('selectAll');"
        "document.execCommand('insertText',false," +
        nlohmann::json(text).dump() + ");}");
    pumpFor(std::chrono::milliseconds(350));
}
} // namespace

int run()
{
    namespace fs = std::filesystem;
    TempVault temp;
    const auto& root = temp.mPath;
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
    replaceEditor(window, "Mine");
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

    put(note, "Baseline");
    window.eval("window.dispatchEvent(new Event('focus'))");
    pumpFor(std::chrono::milliseconds(350));
    replaceEditor(window, "Local for keep");
    put(note, "Outside keep");
    window.eval("window.dispatchEvent(new Event('focus'))");
    pumpFor(std::chrono::milliseconds(350));
    window.eval(
        "probe(JSON.stringify({text:document.querySelector('.cm-content').textContent,"
        "conflict:!document.querySelector('#conflict').hidden}))");
    pumpFor(std::chrono::milliseconds(150));
    check(
        observed.find("Local for keep") != std::string::npos &&
            observed.find("\\\"conflict\\\":true") != std::string::npos,
        "Outside change lost unsaved edits before Keep mine");
    window.eval("document.querySelector('#keep-mine').click()");
    pumpFor(std::chrono::milliseconds(350));
    check(bytes(note) == "Local for keep", "Keep mine did not save the reported version");
    window.eval(
        "probe(JSON.stringify({dirty:!document.querySelector('#dirty').hidden,"
        "conflict:!document.querySelector('#conflict').hidden}))");
    pumpFor(std::chrono::milliseconds(150));
    check(
        observed.find("\\\"dirty\\\":false") != std::string::npos &&
            observed.find("\\\"conflict\\\":false") != std::string::npos,
        "Keep mine did not resolve conflict");

    replaceEditor(window, "Local for reload");
    put(note, "Outside reload");
    window.eval("window.dispatchEvent(new Event('focus'))");
    pumpFor(std::chrono::milliseconds(350));
    window.eval("probe(document.querySelector('.cm-content').textContent)");
    pumpFor(std::chrono::milliseconds(150));
    check(
        observed.find("Local for reload") != std::string::npos,
        "Outside change lost unsaved edits before Reload");
    window.eval("document.querySelector('#reload').click()");
    pumpFor(std::chrono::milliseconds(350));
    window.eval(
        "probe(JSON.stringify({text:document.querySelector('.cm-content').textContent,"
        "dirty:!document.querySelector('#dirty').hidden}))");
    pumpFor(std::chrono::milliseconds(150));
    check(
        observed.find("Outside reload") != std::string::npos &&
            observed.find("\\\"dirty\\\":false") != std::string::npos &&
            bytes(note) == "Outside reload",
        "Reload did not select disk version without writing");

    replaceEditor(window, "Local for discard");
    fs::remove(note);
    window.eval("window.dispatchEvent(new Event('focus'))");
    pumpFor(std::chrono::milliseconds(350));
    window.eval(
        "probe(JSON.stringify({text:document.querySelector('.cm-content').textContent,"
        "missing:!document.querySelector('#missing').hidden}))");
    pumpFor(std::chrono::milliseconds(150));
    check(
        observed.find("Local for discard") != std::string::npos &&
            observed.find("\\\"missing\\\":true") != std::string::npos,
        "Missing note lost unsaved edits before Discard");
    window.eval("document.querySelector('#discard').click()");
    pumpFor(std::chrono::milliseconds(350));
    window.eval("window.dispatchEvent(new Event('focus'))");
    pumpFor(std::chrono::milliseconds(350));
    window.eval(
        "probe(JSON.stringify({text:document.querySelector('.cm-content').textContent,"
        "missing:!document.querySelector('#missing').hidden,"
        "dirty:!document.querySelector('#dirty').hidden}))");
    pumpFor(std::chrono::milliseconds(150));
    check(
        observed.find("Outside reload") != std::string::npos &&
            observed.find("\\\"missing\\\":false") != std::string::npos &&
            observed.find("\\\"dirty\\\":false") != std::string::npos && !fs::exists(note),
        "Discard did not resolve the missing note without writing");
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
