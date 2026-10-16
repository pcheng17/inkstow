#include <doctest/doctest.h>

#include "Bridge.h"
#include "DesktopWindow.h"
#include "LocalHtml.h"
#include "NavigationPolicy.h"
#include "WebviewHeader.h"

#include <nlohmann/json.hpp>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
void activateMacMenu(webview::webview& window, const char* action);
void activateMacClose(webview::webview& window);
void activateMacApplicationQuit();
#elif defined(_WIN32)
#include <windows.h>
#else
#include <glib.h>
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
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

void activateMenu(webview::webview& window, const char* action)
{
    constexpr std::array<std::string_view, 3> kActions = {"open", "save", "quit"};
    const auto index = std::find(kActions.begin(), kActions.end(), action) - kActions.begin();
    check(index < static_cast<std::ptrdiff_t>(kActions.size()), "Unknown native menu action");
#if defined(__APPLE__)
    activateMacMenu(window, action);
#elif defined(_WIN32)
    SendMessageW(
        static_cast<HWND>(window.window().value()),
        WM_COMMAND,
        static_cast<WPARAM>(1001 + index),
        0);
#else
    auto* native = GTK_WIDGET(window.window().value());
    auto* layout = gtk_bin_get_child(GTK_BIN(native));
    GList* children = gtk_container_get_children(GTK_CONTAINER(layout));
    auto* bar = GTK_WIDGET(children->data);
    g_list_free(children);
    children = gtk_container_get_children(GTK_CONTAINER(bar));
    auto* file = GTK_WIDGET(children->data);
    g_list_free(children);
    auto* menu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(file));
    children = gtk_container_get_children(GTK_CONTAINER(menu));
    auto* item = GTK_WIDGET(g_list_nth_data(children, static_cast<guint>(index)));
    g_list_free(children);
    check(item != nullptr, "Native file menu action missing");
    gtk_menu_item_activate(GTK_MENU_ITEM(item));
#endif
}

void replaceEditor(webview::webview& window, const std::string& text)
{
    // Update the editable line and dispatch input through CodeMirror's real DOM observer.
    window.eval(
        "{const line=document.querySelector('#editor .cm-line');"
        "line.textContent=" +
        nlohmann::json(text).dump() +
        ";"
        "document.querySelector('#editor .cm-content').dispatchEvent("
        "new Event('input',{bubbles:true}));}");
    pumpFor(std::chrono::milliseconds(350));
}
} // namespace

TEST_CASE("WebviewWindow protects edits and exercises native window actions")
{
    namespace fs = std::filesystem;
    TempVault temp;
    const auto& root = temp.mPath;
    const auto note = root / "Note.md";
    put(note, "Base");
    struct Shell final : inkstow::webview_shell::ShellServices
    {
        std::string mFolder;
        std::string mLast;
        std::string mAttachment;
        int mPickerCalls = 0;
        std::string lastVault() override { return mLast; }
        void setLastVault(const std::string& folder) override { mLast = folder; }
        std::string pickVault() override
        {
            ++mPickerCalls;
            return mFolder;
        }
        bool openAttachment(const fs::path& path) override
        {
            mAttachment = pathBytes(path);
            return true;
        }
        void watchVault(const std::string& folder) override { (void)folder; }
        void watchNote(const std::string& note) override { (void)note; }
    } shell;
    shell.mFolder = pathBytes(root);
    fs::path openedAttachment;
    bool trashAvailable = false;
    inkstow::webview_shell::Bridge bridge(
        &shell,
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
    // The visible button remains enabled while refresh locks the editor. Click before
    // the refresh bridge reply, as a user can when the native bridge is slow.
    window.eval("window.inkstowRefresh();document.querySelector('#keep-mine').click()");
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

    replaceEditor(window, "Local for changed keep");
    put(note, "First outside keep");
    window.eval("window.dispatchEvent(new Event('focus'))");
    pumpFor(std::chrono::milliseconds(350));
    window.eval("probe(JSON.stringify({conflict:!document.querySelector('#conflict').hidden}))");
    pumpFor(std::chrono::milliseconds(150));
    check(
        observed.find("\\\"conflict\\\":true") != std::string::npos,
        "Changed-version Keep mine did not show initial conflict");
    put(note, "Second outside keep");
    window.eval("window.inkstowRefresh();document.querySelector('#keep-mine').click()");
    pumpFor(std::chrono::milliseconds(500));
    check(bytes(note) == "Second outside keep", "Keep mine overwrote an unseen outside version");
    window.eval(
        "probe(JSON.stringify({conflict:!document.querySelector('#conflict').hidden,"
        "error:document.querySelector('#error').textContent}))");
    pumpFor(std::chrono::milliseconds(150));
    check(
        observed.find("\\\"conflict\\\":true") != std::string::npos &&
            observed.find("review the new conflict") != std::string::npos,
        "Changed outside version did not ask for a new Keep mine decision");
    clickDecision(window, "#keep-mine");
    for (int attempt = 0; attempt < 50 && bytes(note) != "Local for changed keep"; ++attempt)
        pumpFor(std::chrono::milliseconds(100));
    check(bytes(note) == "Local for changed keep", "Reconfirmed Keep mine did not save edits");

    replaceEditor(window, "Local for modal keep");
    put(note, "Outside modal keep");
    window.eval("window.dispatchEvent(new Event('focus'))");
    pumpFor(std::chrono::milliseconds(350));
    window.eval(
        "window.inkstowRefresh();document.querySelector('#keep-mine').click();"
        "window.inkstowAskQuit()");
    pumpFor(std::chrono::milliseconds(500));
    window.eval("probe(JSON.stringify({prompt:!document.querySelector('#quit-prompt').hidden}))");
    pumpFor(std::chrono::milliseconds(150));
    check(
        bytes(note) == "Outside modal keep" &&
            observed.find("\\\"prompt\\\":true") != std::string::npos,
        "Keep mine replayed behind the quit prompt");
    window.eval("document.querySelector('#quit-cancel').click()");
    pumpFor(std::chrono::milliseconds(150));
    clickDecision(window, "#keep-mine");
    for (int attempt = 0; attempt < 50 && bytes(note) != "Local for modal keep"; ++attempt)
        pumpFor(std::chrono::milliseconds(100));
    check(bytes(note) == "Local for modal keep", "Keep mine did not resume after cancel");

    replaceEditor(window, "Local for racing save");
    put(note, "Outside racing save");
    window.eval("window.dispatchEvent(new Event('focus'))");
    pumpFor(std::chrono::milliseconds(350));
    window.eval("probe(JSON.stringify({conflict:!document.querySelector('#conflict').hidden}))");
    pumpFor(std::chrono::milliseconds(150));
    check(
        observed.find("\\\"conflict\\\":true") != std::string::npos,
        "Save race did not show initial conflict");
    // Defer real bridge replies to reproduce Ask Quit waking on the pending edit while
    // refresh has already started its own request. The native bridge still does the work.
    window.eval(
        "window.nativeRequestForRace=window.inkstowRequest;"
        "window.saveRequestsForRace=0;window.saveFinishedForRace=0;"
        "window.inkstowRequest=((nativeRequest)=>async operation=>{"
        "if(operation.operation==='save')window.saveRequestsForRace++;"
        "const reply=await nativeRequest(operation);"
        "if(operation.operation==='save')window.saveFinishedForRace++;"
        "if(operation.operation==='edit' && window.holdEdit)"
        "return new Promise(resolve=>{window.releaseEdit=()=>resolve(reply)});"
        "if(operation.operation==='refresh' && window.holdRefresh)"
        "return new Promise(resolve=>{window.releaseRefresh=()=>resolve(reply)});"
        "return reply})(window.nativeRequestForRace);"
        "window.holdEdit=true;window.holdRefresh=true;");
    replaceEditor(window, "Local for racing save edited");
    window.eval("probe(typeof window.releaseEdit==='function')");
    pumpFor(std::chrono::milliseconds(150));
    check(observed.find("true") != std::string::npos, "Edit reply was not held for Save race");
    window.eval(
        "window.inkstowRefresh();document.querySelector('#keep-mine').click();"
        "window.inkstowAskQuit();window.releaseEdit();window.holdEdit=false");
    for (int attempt = 0; attempt < 50; ++attempt) {
        window.eval(
            "probe(JSON.stringify({prompt:!document.querySelector('#quit-prompt').hidden,"
            "refreshHeld:typeof window.releaseRefresh==='function'}))");
        pumpFor(std::chrono::milliseconds(100));
        if (observed.find("\\\"prompt\\\":true") != std::string::npos &&
            observed.find("\\\"refreshHeld\\\":true") != std::string::npos)
            break;
    }
    check(
        observed.find("\\\"prompt\\\":true") != std::string::npos &&
            observed.find("\\\"refreshHeld\\\":true") != std::string::npos,
        "Quit prompt did not open while refresh reply was held");
    window.eval(
        "document.querySelector('#quit-cancel').click();"
        "document.querySelector('#save').click();"
        "window.releaseRefresh();window.holdRefresh=false");
    for (int attempt = 0; attempt < 50; ++attempt) {
        window.eval(
            "probe(JSON.stringify({saves:window.saveRequestsForRace,"
            "finished:window.saveFinishedForRace}))");
        pumpFor(std::chrono::milliseconds(100));
        if (observed.find("\\\"finished\\\":1") != std::string::npos) break;
    }
    check(
        observed.find("\\\"saves\\\":1") != std::string::npos &&
            observed.find("\\\"finished\\\":1") != std::string::npos,
        "Save did not finish after cancelling the quit prompt");
    pumpFor(std::chrono::milliseconds(200));
    check(
        bytes(note) == "Outside racing save",
        "Queued Keep mine replayed alongside Save after the quit prompt was cancelled");
    window.eval("window.inkstowRequest=window.nativeRequestForRace");

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
    for (int attempt = 0; attempt < 50; ++attempt) {
        window.eval(
            "probe(JSON.stringify({title:document.querySelector('#title').textContent,"
            "ready:!document.querySelector('#follow').disabled}))");
        pumpFor(std::chrono::milliseconds(100));
        if (observed.find("Source.md") != std::string::npos &&
            observed.find("\\\"ready\\\":true") != std::string::npos)
            break;
    }
    check(
        observed.find("Source.md") != std::string::npos &&
            observed.find("\\\"ready\\\":true") != std::string::npos,
        "Source note was not ready for Follow");
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
    if (observed.find("Renamed.md") == std::string::npos) {
        window.eval(
            "probe(JSON.stringify({title:document.querySelector('#title').textContent,"
            "error:document.querySelector('#error').textContent,"
            "disabled:document.querySelector('#follow').disabled}))");
        pumpFor(std::chrono::milliseconds(150));
        throw std::runtime_error("Follow did not navigate: " + observed);
    }
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
    for (int attempt = 0; attempt < 50; ++attempt) {
        window.eval(
            "probe(JSON.stringify({title:document.querySelector('#title').textContent,"
            "ready:!document.querySelector('#trash').disabled}))");
        pumpFor(std::chrono::milliseconds(100));
        if (observed.find("Source.md") != std::string::npos &&
            observed.find("\\\"ready\\\":true") != std::string::npos)
            break;
    }
    check(
        observed.find("Source.md") != std::string::npos &&
            observed.find("\\\"ready\\\":true") != std::string::npos,
        "Source note was not ready for Trash");
    window.eval(
        "document.querySelector('#trash').click();"
        "document.querySelector('#trash-cancel').click()");
    pumpFor(std::chrono::milliseconds(150));
    check(fs::exists(root / "Source.md"), "Cancelled trash removed note");
    clickDecision(window, "#trash");
    pumpFor(std::chrono::milliseconds(150));
    clickDecision(window, "#trash-confirm");
    for (int attempt = 0; attempt < 50; ++attempt) {
        window.eval(
            "probe(JSON.stringify({error:document.querySelector('#error').textContent,"
            "ready:!document.querySelector('#trash').disabled}))");
        pumpFor(std::chrono::milliseconds(100));
        if (observed.find("System trash unavailable") != std::string::npos &&
            observed.find("\\\"ready\\\":true") != std::string::npos)
            break;
    }
    check(
        fs::exists(root / "Source.md") &&
            observed.find("System trash unavailable") != std::string::npos &&
            observed.find("\\\"ready\\\":true") != std::string::npos,
        "Unavailable trash did not refuse removal");
    trashAvailable = true;
    clickDecision(window, "#trash");
    pumpFor(std::chrono::milliseconds(150));
    clickDecision(window, "#trash-confirm");
    for (int attempt = 0; attempt < 30 && fs::exists(root / "Source.md"); ++attempt)
        pumpFor(std::chrono::milliseconds(100));
    check(!fs::exists(root / "Source.md"), "Confirmed trash was not invoked");

    // Exercise native menus with a disposable vault after conflict/recovery scenarios.
    put(note, "[[image.png]]");
    put(root / "image.png", "image");
    activateMenu(window, "open");
    for (int attempt = 0; attempt < 50 && shell.mPickerCalls == 0; ++attempt)
        pumpFor(std::chrono::milliseconds(100));
    check(
        shell.mPickerCalls == 1 && shell.mLast == shell.mFolder,
        "Native Open menu did not invoke the folder picker");
    window.eval("document.querySelector('#pick-vault').click()");
    for (int attempt = 0; attempt < 50 && shell.mPickerCalls == 1; ++attempt)
        pumpFor(std::chrono::milliseconds(100));
    check(shell.mPickerCalls == 2, "Picker control did not use the menu's native service");
    window.eval(
        "[...document.querySelectorAll('#notes button')]"
        ".find(b=>b.textContent==='Note.md').click()");
    for (int attempt = 0; attempt < 50; ++attempt) {
        observed.clear();
        window.eval(
            "probe(JSON.stringify({title:document.querySelector('#title').textContent,"
            "text:document.querySelector('.cm-content').textContent,"
            "ready:!document.querySelector('#follow').disabled && "
            "!!document.querySelector('#editor [contenteditable=true]')}))");
        pumpFor(std::chrono::milliseconds(100));
        if (observed.find("Note.md") != std::string::npos &&
            observed.find("[[image.png]]") != std::string::npos &&
            observed.find("\\\"ready\\\":true") != std::string::npos)
            break;
    }
    check(
        observed.find("Note.md") != std::string::npos &&
            observed.find("[[image.png]]") != std::string::npos &&
            observed.find("\\\"ready\\\":true") != std::string::npos,
        "Note was not ready for native Follow");
    // Delay just this test's link request so a previous attachment opening cannot satisfy the wait.
    window.eval(
        "{const request=window.inkstowRequest;"
        "window.inkstowRequest=async operation=>{"
        "if(operation.operation==='link'){"
        "window.inkstowRequest=request;"
        "await new Promise(resolve=>setTimeout(resolve,450));}"
        "return request(operation);};}");
    openedAttachment.clear();
    window.eval("inkstowFollow()");
    for (int attempt = 0; attempt < 50 && openedAttachment.empty(); ++attempt)
        pumpFor(std::chrono::milliseconds(100));
    check(
        !openedAttachment.empty() && fs::equivalent(openedAttachment, root / "image.png"),
        "Validated attachment did not reach native opener");
    for (int attempt = 0; attempt < 50; ++attempt) {
        observed.clear();
        window.eval(
            "probe(JSON.stringify({title:document.querySelector('#title').textContent,"
            "text:document.querySelector('.cm-content').textContent,"
            "ready:!document.querySelector('#save').disabled && "
            "!!document.querySelector('#editor [contenteditable=true]')}))");
        pumpFor(std::chrono::milliseconds(100));
        if (observed.find("Note.md") != std::string::npos &&
            observed.find("[[image.png]]") != std::string::npos &&
            observed.find("\\\"ready\\\":true") != std::string::npos)
            break;
    }
    check(
        observed.find("Note.md") != std::string::npos &&
            observed.find("[[image.png]]") != std::string::npos &&
            observed.find("\\\"ready\\\":true") != std::string::npos,
        "Editor was not ready after native Follow");
    replaceEditor(window, "Menu saved");
    activateMenu(window, "save");
    for (int attempt = 0; attempt < 50 && bytes(note) != "Menu saved"; ++attempt)
        pumpFor(std::chrono::milliseconds(100));
    if (bytes(note) != "Menu saved") {
        window.eval(
            "probe(JSON.stringify({title:document.querySelector('#title').textContent,"
            "text:document.querySelector('.cm-content').textContent,"
            "error:document.querySelector('#error').textContent,"
            "disabled:document.querySelector('#save').disabled}))");
        pumpFor(std::chrono::milliseconds(150));
        throw std::runtime_error(
            "Native Save menu did not invoke editor save: " + observed + ", disk=" + bytes(note));
    }
    replaceEditor(window, "Quit draft");
#if defined(_WIN32)
    SendMessageW(static_cast<HWND>(window.window().value()), WM_CLOSE, 0, 0);
#elif defined(__APPLE__)
    activateMacClose(window);
#else
    gtk_window_close(GTK_WINDOW(window.window().value()));
#endif
    pumpFor(std::chrono::milliseconds(350));
    window.eval(
        "probe(JSON.stringify({prompt:!document.querySelector('#quit-prompt').hidden,"
        "focused:document.activeElement.id,text:document.querySelector('.cm-content').textContent})"
        ")");
    pumpFor(std::chrono::milliseconds(150));
    if (observed.find("\\\"prompt\\\":true") == std::string::npos ||
        observed.find("quit-cancel") == std::string::npos ||
        observed.find("Quit draft") == std::string::npos || exited)
    {
        throw std::runtime_error(
            "Native close did not protect unsaved edits with Cancel as default: " + observed +
            ", exited=" + std::to_string(exited));
    }
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
#elif defined(__APPLE__)
    auto promptVisible = [&] {
        window.eval(
            "probe(JSON.stringify({prompt:!document.querySelector('#quit-prompt').hidden,"
            "text:document.querySelector('.cm-content').textContent}))");
        pumpFor(std::chrono::milliseconds(150));
        return observed.find("\\\"prompt\\\":true") != std::string::npos &&
               observed.find("Quit draft") != std::string::npos;
    };
    activateMenu(window, "quit"); // Cmd+Q through the native App menu.
    pumpFor(std::chrono::milliseconds(350));
    check(promptVisible() && !exited, "Cmd+Q did not protect unsaved text");
    window.eval("document.querySelector('#quit-cancel').click()");
    pumpFor(std::chrono::milliseconds(150));
    activateMacApplicationQuit(); // Also guard Dock / OS application termination.
    pumpFor(std::chrono::milliseconds(350));
    check(promptVisible() && !exited, "OS application quit bypassed safe close");
    window.eval("document.querySelector('#quit-cancel').click()");
    pumpFor(std::chrono::milliseconds(150));
    put(note, "Outside quit");
    activateMenu(window, "quit");
    pumpFor(std::chrono::milliseconds(350));
    window.eval("document.querySelector('#quit-save').click()");
    pumpFor(std::chrono::milliseconds(350));
    check(
        promptVisible() && !exited && bytes(note) == "Outside quit",
        "Conflicted Cmd+Q save lost edits or closed the window");
    window.eval("document.querySelector('#quit-cancel').click()");
    pumpFor(std::chrono::milliseconds(150));
    clickDecision(window, "#reload");
    pumpFor(std::chrono::milliseconds(350));
    replaceEditor(window, "Saved on quit");
    activateMenu(window, "quit");
    pumpFor(std::chrono::milliseconds(350));
    window.eval("document.querySelector('#quit-save').click()");
    pumpFor(std::chrono::milliseconds(350));
    check(exited && bytes(note) == "Saved on quit", "Cmd+Q Save and quit failed");
#endif
}
