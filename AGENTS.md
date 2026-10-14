# Inkstow agent workflow

Make each change on a new branch in its own git worktree, and merge it through a pull request. Never commit or push to `main`.

For changes to the vault core or UI, run the CMake build and CTest commands in README's "Install and run" section before reporting success. The default build produces only the app; build `unit-tests` and `ui-tests` explicitly before CTest. For any C++ change, format with `clang-format -i` version 21 or newer (configured by `.clang-format`; older versions reformat correct code), keep the build free of compiler warnings, and after `cmake --preset debug -DINKSTOW_BUILD_WEBVIEW=ON` (even if a cached build was Qt-only) run `clang-tidy -p build/debug src/*.cpp src/core/*.cpp src/ui/*.cpp tests/*.cpp` from the repo root (configured by `.clang-tidy`). Also check `src/core/portable/*.cpp` and Linux `src/webview/Bridge.cpp src/webview/Main.cpp src/webview/NavigationPolicyLinux.cpp` where those sources have compilation entries. Resolve clang-tidy findings before reporting success; if the check cannot run or fails, report that explicitly.

Use `tests/fixtures/sample` or a throwaway vault for verification, not a personal vault. Add small regression fixtures for newly discovered Obsidian behaviors. For portable migration work, use `src/core/portable/`; for Qt-specific core work, use the existing `src/core/Vault.*` and `src/core/Session.*`. Keep vault operations in Vault and UI-independent state in Session, testable without a display. Follow README's "Portable open/save migration (#88)" and "Source and test layout" for boundaries and tests; consult its "Boundaries and limitations" for current link behavior and save caveats.

See README for setup and manual smoke steps.

## Agent skills

### Issue tracker

Issues are tracked in GitHub Issues on `pcheng17/inkstow` via the `gh` CLI. See `docs/agents/issue-tracker.md`.

### Triage labels

Uses the five default triage labels (`needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`). See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: one `GLOSSARY.md` and `docs/adr/` at the repo root. See `docs/agents/domain.md`.
