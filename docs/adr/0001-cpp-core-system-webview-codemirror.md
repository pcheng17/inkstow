---
status: proposed
---

# C++ core + system webview + CodeMirror 6

Inkstow aims to match Obsidian's best editing behavior while keeping the shipped app as small as possible. Most of that behavior (Live Preview, inline wikilink rendering, autocomplete, folding, correct IME and accessibility) lives in Obsidian's editor, CodeMirror 6, not in its vault logic. We decided to keep the vault core in C++, host the UI in the operating system's own webview (WKWebView on macOS, WebView2 on Windows, WebKitGTK on Linux) through a thin C++ shell such as [webview/webview](https://github.com/webview/webview), and use CodeMirror 6 as the editor. This reuses the tested C++ core and its Obsidian-compatibility fixtures, gets an Obsidian-class editor without writing a text engine, and ships no bundled GUI toolkit or browser engine. The Qt Widgets app and duplicate Qt core have been retired in favor of this architecture. The maintainer chose retirement without requiring feature-by-feature Qt parity; remaining gaps must be documented rather than hidden.

## Considered Options

- **Keep Qt Widgets.** Already works, but bundles are tens of MB even after pruning (#71), and Live Preview would have to be hand-built on `QTextDocument`, the largest and riskiest piece of work in the project.
- **A webview shell in another language.** Similar webview-and-JS shape and size, but rewriting the core and its behavior would leave the C++ tests as the reference until the rewrite reaches parity.
- **Native UI per platform (AppKit / Win32 / GTK).** The smallest result, but it means three UIs and three text-editor integrations to build and keep in step.
- **Dear ImGui or a custom-drawn UI.** Very small, but weak IME, accessibility, and text selection, which a notes editor cannot do without.
- **Go + Wails, or Swift.** Wails has the same core rewrite cost. Swift is macOS-only.

## Consequences

- **The shipped core is Qt-free.** `src/core/vault/` implements saves, trash, UTF-8 decoding, paths and links with standard C++ and small platform adapters. The former Qt Core implementation was retired. The README's "Save conflict guarantee" and Windows retry-on-lock limitations still apply.
- **UI-layer services live in the shell.** File watching, last-vault settings, attachment opening, native folder picking, and menus with shortcuts (including macOS Cmd+Q with save/cancel) use C++ shell and platform code rather than Qt services.
- **A JS frontend joins the codebase.** CodeMirror 6 and the UI are bundled as static assets embedded in or shipped beside the binary, loaded with no network access. The C++/JS bridge becomes a real interface: keep vault rules and conflict decisions in C++, and let the frontend only present them.
- **The three platform webviews differ.** WebKitGTK (Linux), WebView2 (Windows, which depends on the Evergreen runtime being present), and WKWebView (macOS) behave differently. CI must launch and smoke-test the app on every release platform, as the Qt bundles already do.
- **Tests span two runners.** `inkstow-tests` holds headless core/session, bridge and native-window C++ cases (CTest supplies Xvfb for Linux window cases); Node tests cover CodeMirror editing and Live Preview. The former Qt `highlighter-tests` and `ui-tests` are retired, without claiming every Qt-only assertion has a WebView equivalent.
- **Size claims need measurement.** The expected result (a few MB of binary plus about 1 MB of JS) remains an estimate. The release CI reports compressed archives and unpacked shipped bytes per platform, with external system WebKitGTK, WKWebView, WebView2 Evergreen and Windows C++ runtime excluded and disclosed. Record actual platform measurements and smoke evidence before accepting this ADR; removing Qt alone is not proof of a size benefit.
