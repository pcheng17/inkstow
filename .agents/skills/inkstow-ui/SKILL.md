---
name: inkstow-ui
description: Run the real Inkstow system-webview window with a disposable vault on a headless X11 display and capture a screenshot. Use when changing how the Inkstow UI looks or behaves, when asked to run, show, or screenshot the app, or to check an end-to-end workflow in the real window.
---

# Check and screenshot the WebView UI

Use a throwaway vault, never a personal vault. The old Qt `scripts/inkstow-headless.sh` helper has been retired; the supported automated window driver is the `WebviewWindow*` doctest case in `inkstow-tests`.

1. **Build the app and tests.** From the active worktree run `cmake --preset debug`, `cmake --build --preset debug --parallel`, and `cmake --build --preset debug --target unit-tests --parallel`. Completion: `build/debug/inkstow-webview` and `build/debug/tests/inkstow-tests` exist.
2. **Exercise the real window.** Run `ctest --preset debug --output-on-failure`. On Linux this runs native-navigation and WebView window cases under Xvfb. To run only the window case and save its screenshot:

   ```sh
   INKSTOW_SCREENSHOT=/tmp/inkstow-webview.png xvfb-run -a \
     ./build/debug/tests/inkstow-tests --test-case=WebviewWindow*
   ```

   The case creates its own disposable vault, checks menu/picker/attachment and conflict/quit flows with injected native services, and writes the screenshot when requested. Completion: the case passes and the PNG exists.
3. **Inspect the screenshot.** Read the PNG yourself before describing what it shows. A passing test is behavior evidence; a screenshot is visual evidence, not proof of correctness. Compare images from separate worktrees only after building and running the same test on both commits. Leave scratch screenshots outside the repository unless asked to commit them.
4. **Manual platform smoke.** For real OS folder dialogs and default attachment handlers, copy `tests/fixtures/sample` to a temporary vault and launch `inkstow-webview` on the target desktop. Follow the README's WebView smoke steps, including native picker, edit/save, watcher, conflict and quit; do not claim this manual check from the injected-service test alone.

The headless Linux run needs `xvfb-run` and WebKitGTK. macOS and Windows real-window cases run through CTest without Xvfb. If the screenshot variable is unsupported in a particular test revision, treat the passing CTest run as behavioral evidence and state that no screenshot was captured.
