# Inkstow agent workflow

Make each change on a new branch in its own git worktree, and merge it through a pull request. Never commit or push to `main`.

For changes to the vault core or UI, run the CMake build and CTest commands in README's "Install and run" section before reporting success. The default build produces only the app; build `unit-tests` and `ui-tests` explicitly before CTest. For any C++ change, format with `clang-format -i` version 21 or newer (configured by `.clang-format`; older versions reformat correct code), keep the build free of compiler warnings, and after `cmake --preset debug -DINKSTOW_BUILD_WEBVIEW=ON` (even if a cached build was Qt-only) run `clang-tidy -p build/debug src/*.cpp src/core/*.cpp src/ui/*.cpp tests/*.cpp` from the repo root (configured by `.clang-tidy`). Also check `src/core/portable/*.cpp` and Linux `src/webview/Bridge.cpp src/webview/Main.cpp src/webview/NavigationPolicyLinux.cpp` where those sources have compilation entries. Resolve clang-tidy findings before reporting success; if the check cannot run or fails, report that explicitly.

Use `tests/fixtures/sample` or a throwaway vault for verification, not a personal vault. Add small regression fixtures for newly discovered Obsidian behaviors. The current core treats note content as opaque Markdown; link resolution and live preview are not implemented, and save conflict detection has an external-write race. Follow README's "Source and test layout" when adding files or tests: keep vault operations in `src/core/Vault.*` and UI-independent state in `src/core/Session.*`, testable without a display.

See README for setup, manual smoke steps, and current limitations.

## Agent skills

### Issue tracker

Issues are tracked in GitHub Issues on `pcheng17/inkstow` via the `gh` CLI. See `docs/agents/issue-tracker.md`.

### Triage labels

Uses the five default triage labels (`needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`). See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: one `GLOSSARY.md` and `docs/adr/` at the repo root. See `docs/agents/domain.md`.
