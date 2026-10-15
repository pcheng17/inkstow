#include "Bridge.h"
#include "DesktopWindow.h"
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
#include <gtk/gtk.h>
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

void clickDecision(webview::webview& window, const char* selector)
{
    window.eval(
        "{const button=document.querySelector(" + nlohmann::json(selector).dump() +
        ");function "
        "click(){if(button.disabled){setTimeout(click,25);return;}button.click();}click();}");
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
    inkstow::webview_shell::DesktopWindow desktop(window);
    bool exited = false;
    window.bind("inkstowExit", [&exited](const std::string&) {
        exited = true;
        return std::string("null");
    });
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
    window.eval(
        "probe(JSON.stringify({error:document.querySelector('#error').textContent,"
        "text:document.querySelector('.cm-content').textContent}))");
    pumpFor(std::chrono::milliseconds(150));
    check(
        observed.find("Mine") != std::string::npos &&
            observed.find("changed on disk") != std::string::npos,
        "Stale save did not explain the conflict or retain unsaved text");
    clickDecision(window, "#merge");
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
    window.eval(
        "{const editor=document.querySelector('#editor [contenteditable=true]');"
        "editor.focus();document.execCommand('selectAll');"
        "document.execCommand('insertText',false,'Resolved merge');}");
    pumpFor(std::chrono::milliseconds(350));
    window.eval("probe(document.querySelector('.cm-content').textContent)");
    pumpFor(std::chrono::milliseconds(150));
    check(observed.find("Resolved merge") != std::string::npos, "Merge markers were not editable");
    fs::remove(note);
    window.eval("window.dispatchEvent(new Event('focus'));");
    pumpFor(std::chrono::milliseconds(350));
    window.eval(
        "probe(JSON.stringify({missing:!document.querySelector('#missing').hidden,"
        "text:document.querySelector('.cm-content').textContent}));");
    pumpFor(std::chrono::milliseconds(150));
    check(
        observed.find("\\\"missing\\\":true") != std::string::npos &&
            observed.find("Resolved merge") != std::string::npos,
        "Missing note lost resolved merge edits");
    put(note, "Reappeared");
    clickDecision(window, "#save-as-new");
    pumpFor(std::chrono::milliseconds(350));
    check(bytes(note) == "Reappeared", "Save as new overwrote reappeared file");
    window.eval(
        "probe(JSON.stringify({error:document.querySelector('#error').textContent,"
        "text:document.querySelector('.cm-content').textContent}));");
    pumpFor(std::chrono::milliseconds(150));
    check(
        observed.find("Resolved merge") != std::string::npos &&
            observed.find("error") != std::string::npos &&
            observed.find("exists") != std::string::npos,
        "Refused recovery did not explain error or retain editor text");
    fs::remove(note);
    clickDecision(window, "#save-as-new");
    pumpFor(std::chrono::milliseconds(350));
    check(
        bytes(note) == "Resolved merge",
        "Recovery did not save resolved merge text as a new note");
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
    clickDecision(window, "#keep-mine");
    for (int attempt = 0; attempt < 50 && bytes(note) != "Local for keep"; ++attempt) {
        pumpFor(std::chrono::milliseconds(100));
    }
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
    clickDecision(window, "#reload");
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
    clickDecision(window, "#discard");
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
    replaceEditor(window, "Quit draft");
#if defined(_WIN32)
    SendMessageW(static_cast<HWND>(window.window().value()), WM_CLOSE, 0, 0);
#elif defined(__APPLE__)
    desktop.invoke("inkstowAskQuit()");
#else
    gtk_window_close(GTK_WINDOW(window.window().value()));
#endif
    pumpFor(std::chrono::milliseconds(350));
    window.eval(
        "probe(JSON.stringify({prompt:!document.querySelector('#quit-prompt').hidden,"
        "focused:document.activeElement.id,text:document.querySelector('.cm-content').textContent})"
        ")");
    pumpFor(std::chrono::milliseconds(150));
    check(
        observed.find("\\\"prompt\\\":true") != std::string::npos &&
            observed.find("quit-cancel") != std::string::npos &&
            observed.find("Quit draft") != std::string::npos && !exited,
        "Native close did not protect unsaved edits with Cancel as default");
    window.eval("document.querySelector('#quit-cancel').click()");
    pumpFor(std::chrono::milliseconds(150));
    check(!exited, "Cancel terminated the window");
#if defined(_WIN32)
    // The webview owns GetMessage; use the real queue so the thread's native accelerator hook runs.
    BYTE originalKeys[256]{};
    check(GetKeyboardState(originalKeys) != 0, "Could not read keyboard state");
    BYTE shortcutKeys[256]{};
    std::copy(std::begin(originalKeys), std::end(originalKeys), std::begin(shortcutKeys));
    shortcutKeys[VK_CONTROL] |= 0x80;
    check(SetKeyboardState(shortcutKeys) != 0, "Could not set keyboard state");
    check(
        PostMessageW(static_cast<HWND>(window.window().value()), WM_KEYDOWN, 'Q', 0) != 0,
        "Could not queue Ctrl+Q");
    pumpFor(std::chrono::milliseconds(350));
    SetKeyboardState(originalKeys);
    window.eval(
        "probe(JSON.stringify({prompt:!document.querySelector('#quit-prompt').hidden,"
        "text:document.querySelector('.cm-content').textContent}))");
    pumpFor(std::chrono::milliseconds(150));
    check(
        observed.find("\\\"prompt\\\":true") != std::string::npos &&
            observed.find("Quit draft") != std::string::npos && !exited,
        "Native Ctrl+Q did not offer safe quit");
#endif
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
