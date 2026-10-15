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
#include <webkit2/webkit2.h>
#endif

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
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
    fs::path openedAttachment;
    bool trashAvailable = false;
    inkstow::webview_shell::Bridge bridge(
        [&](const fs::path& path, std::string*) {
            openedAttachment = path;
            return true;
        },
        [&](const fs::path& path, std::string* error) {
            if (!trashAvailable) {
                *error = "System trash unavailable.";
                return false;
            }
            return fs::remove(path);
        });
    webview::webview window(false, nullptr);
    window.set_size(960, 720, WEBVIEW_HINT_NONE);
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

    put(root / "Source.md",
        "# Preview\n**bold** and *italic* with `code`\n- item\n> quote\n[[Target]] [[image.png]]");
    put(root / "Target.md", "Target");
    put(root / "image.png", "image");
    window.eval("document.querySelector('#vault-form button').click()");
    pumpFor(std::chrono::milliseconds(350));
    window.eval(
        "document.querySelector('#new-path').value='Target';"
        "document.querySelector('#create-form button').click()");
    pumpFor(std::chrono::milliseconds(350));
    check(bytes(root / "Target.md") == "Target", "Create overwrote existing note");
    window.eval(
        "document.querySelector('#new-path').value='Fresh';"
        "document.querySelector('#create-form button').click()");
    pumpFor(std::chrono::milliseconds(350));
    check(fs::exists(root / "Fresh.md"), "Create control did not create note");
    window.eval(
        "document.querySelector('#move').click();"
        "document.querySelector('#move-path').value='Cancelled';"
        "document.querySelector('#move-cancel').click()");
    pumpFor(std::chrono::milliseconds(150));
    check(!fs::exists(root / "Cancelled.md"), "Cancelled move wrote a note");
    window.eval(
        "document.querySelector('#move').click();"
        "document.querySelector('#move-path').value='Moved';"
        "document.querySelector('#move-without-links').click()");
    pumpFor(std::chrono::milliseconds(350));
    check(fs::exists(root / "Moved.md") && !fs::exists(root / "Fresh.md"), "Move failed");
    window.eval(
        "[...document.querySelectorAll('#notes button')]"
        ".find(b=>b.textContent==='Target.md').click()");
    pumpFor(std::chrono::milliseconds(350));
    window.eval(
        "document.querySelector('#move').click();"
        "document.querySelector('#move-path').value='Renamed';"
        "document.querySelector('#move-with-links').click()");
    pumpFor(std::chrono::milliseconds(350));
    check(
        bytes(root / "Source.md").find("[[/Renamed]]") != std::string::npos,
        "Move with links did not rewrite supported link");
    window.eval(
        "[...document.querySelectorAll('#notes button')]"
        ".find(b=>b.textContent==='Source.md').click()");
    pumpFor(std::chrono::milliseconds(350));
    window.eval(
        "probe(JSON.stringify({link:document.querySelector('.cm-preview-link')?.textContent,"
        "heading:document.querySelector('.cm-line')?.textContent,"
        "bold:document.querySelector('.cm-strong')?.textContent,"
        "source:document.querySelectorAll('.cm-line')[4]?.textContent}))");
    pumpFor(std::chrono::milliseconds(150));
    check(
        observed.find("Renamed") != std::string::npos && observed.find("[[") == std::string::npos &&
            observed.find("Preview") != std::string::npos &&
            observed.find("bold") != std::string::npos,
        "Real window did not render an inactive wikilink");
#if !defined(__APPLE__) && !defined(_WIN32)
    if (const char* screenshot = std::getenv("INKSTOW_SCREENSHOT")) {
        struct Capture
        {
            std::string mPath;
            bool mDone = false;
            bool mSaved = false;
        };
        auto capture = std::make_shared<Capture>(screenshot);
        auto* cancellable = g_cancellable_new();
        webkit_web_view_get_snapshot(
            WEBKIT_WEB_VIEW(window.browser_controller().value()),
            WEBKIT_SNAPSHOT_REGION_VISIBLE,
            WEBKIT_SNAPSHOT_OPTIONS_NONE,
            cancellable,
            +[](GObject* object, GAsyncResult* result, gpointer data) {
                std::unique_ptr<std::shared_ptr<Capture>> callbackState(
                    static_cast<std::shared_ptr<Capture>*>(data));
                const auto& output = *callbackState;
                GError* failure = nullptr;
                auto* surface =
                    webkit_web_view_get_snapshot_finish(WEBKIT_WEB_VIEW(object), result, &failure);
                if (surface) {
                    output->mSaved = cairo_surface_write_to_png(surface, output->mPath.c_str()) ==
                                     CAIRO_STATUS_SUCCESS;
                    cairo_surface_destroy(surface);
                }
                if (failure) g_error_free(failure);
                output->mDone = true;
            },
            new std::shared_ptr<Capture>(capture));
        for (int attempt = 0; !capture->mDone && attempt < 50; ++attempt)
            pumpFor(std::chrono::milliseconds(100));
        if (!capture->mDone) g_cancellable_cancel(cancellable);
        g_object_unref(cancellable);
        check(capture->mSaved, "Could not save real webview screenshot");
    }
#endif
    window.eval(
        "{function clickLink(){if(document.querySelector('#follow').disabled){"
        "setTimeout(clickLink,25);return;}document.querySelector('.cm-preview-link').click();}"
        "clickLink();}");
    for (int attempt = 0; attempt < 30; ++attempt) {
        window.eval("probe(document.querySelector('#title').textContent)");
        pumpFor(std::chrono::milliseconds(100));
        if (observed.find("Renamed.md") != std::string::npos) break;
    }
    check(observed.find("Renamed.md") != std::string::npos, "Follow did not navigate");
    window.eval(
        "[...document.querySelectorAll('#notes button')]"
        ".find(b=>b.textContent==='Source.md').click()");
    pumpFor(std::chrono::milliseconds(350));
    window.eval(
        "document.querySelector('#link').value='image.png';"
        "document.querySelector('#open-attachment').click()");
    pumpFor(std::chrono::milliseconds(350));
    check(
        !openedAttachment.empty() && fs::equivalent(openedAttachment, root / "image.png"),
        "Attachment did not use shell service");
    window.eval(
        "document.querySelector('#trash').click();"
        "document.querySelector('#trash-cancel').click()");
    pumpFor(std::chrono::milliseconds(150));
    check(fs::exists(root / "Source.md"), "Cancelled trash removed note");
    window.eval(
        "document.querySelector('#trash').click();"
        "document.querySelector('#trash-confirm').click()");
    pumpFor(std::chrono::milliseconds(350));
    check(fs::exists(root / "Source.md"), "Unavailable trash removed note");
    trashAvailable = true;
    clickDecision(window, "#trash");
    pumpFor(std::chrono::milliseconds(150));
    clickDecision(window, "#trash-confirm");
    for (int attempt = 0; attempt < 30 && fs::exists(root / "Source.md"); ++attempt)
        pumpFor(std::chrono::milliseconds(100));
    check(!fs::exists(root / "Source.md"), "Confirmed trash was not invoked");
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
