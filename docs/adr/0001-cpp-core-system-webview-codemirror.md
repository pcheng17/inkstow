---
status: proposed
---

# C++ core + system webview + CodeMirror 6

Inkstow aims to match Obsidian's best editing behavior while keeping the shipped app as small as possible. Most of that behavior (Live Preview, inline wikilink rendering, autocomplete, folding, correct IME and accessibility) lives in Obsidian's editor, CodeMirror 6, not in its vault logic. We decided to keep the vault core in C++, host the UI in the operating system's own webview (WKWebView on macOS, WebView2 on Windows, WebKitGTK on Linux) through a thin C++ shell such as [webview/webview](https://github.com/webview/webview), and use CodeMirror 6 as the editor. This reuses the tested C++ core and its Obsidian-compatibility fixtures, gets an Obsidian-class editor without writing a text engine, and ships no bundled GUI toolkit or browser engine. This replaces both the Qt Widgets UI and the Rust/Tauri preview in `rust/` (#74) as the target architecture.

## Considered Options

- **Keep Qt Widgets.** Already works, but bundles are tens of MB even after pruning (#71), and Live Preview would have to be hand-built on `QTextDocument`, the largest and riskiest piece of work in the project.
- **Rust + Tauri (the `rust/` preview).** Same webview-and-JS shape as this decision, with a similar size. Rejected because it means rewriting the core and its behavior in a second language, with the C++ tests remaining the reference until that rewrite reaches parity.
- **Native UI per platform (AppKit / Win32 / GTK).** The smallest result, but it means three UIs and three text-editor integrations to build and keep in step.
- **Dear ImGui or a custom-drawn UI.** Very small, but weak IME, accessibility, and text selection, which a notes editor cannot do without.
- **Go + Wails, or Swift.** Wails has the same rewrite cost as Rust. Swift is macOS-only.

## Consequences

- **The core must drop Qt Core to reach the size goal.** `src/core/` currently links `Qt6::Core` and depends on it for atomic saves (`QSaveFile`), system trash (`QFile::moveToTrash`), UTF-8 decoding with BOM handling (`QStringDecoder`), path handling (`QDir`, `QFileInfo`), and regex (`QRegularExpression`). Keeping Qt Core would retain several MB of runtime. Porting these to the standard library plus small per-platform code is a prerequisite, and it must preserve the guarantees in README's "Save conflict guarantee" section, including the Windows retry-on-lock behavior.
- **Some UI-layer services move to the shell.** File watching (`QFileSystemWatcher`), settings persistence such as the last vault (`QSettings`), opening attachments (`QDesktopServices`), the native folder picker, and app menus with shortcuts (including the macOS Cmd+Q path, which must offer to save) have to be provided by the C++ shell or by per-platform code.
- **A JS frontend joins the codebase.** CodeMirror 6 and the UI are bundled as static assets embedded in or shipped beside the binary, loaded with no network access. The C++/JS bridge becomes a real interface: keep vault rules and conflict decisions in C++, and let the frontend only present them. Much of `rust/web/` can carry over.
- **The three platform webviews differ.** WebKitGTK (Linux), WebView2 (Windows, which depends on the Evergreen runtime being present), and WKWebView (macOS) behave differently. CI must launch and smoke-test the app on every release platform, as the Qt bundles already do.
- **The test split changes.** Core and session tests stay headless C++. Editor behavior such as highlighting and Live Preview moves to JS tests, replacing `highlighter-tests`. The window-level coverage in `ui-tests` has to be rebuilt against the webview shell.
- **Size claims need measurement.** The expected result (a few MB of binary plus about 1 MB of JS) is an estimate. Record measured bundle sizes per platform before treating this ADR as accepted.
