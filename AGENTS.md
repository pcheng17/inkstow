# Inkstow agent workflow

Make each change on a new branch in its own git worktree, and merge it through a pull request. Never commit or push to `main`.

For changes to the vault core or UI, run the CMake build and CTest commands in README's "Install and run" section before reporting success. The default build produces the WebView app; build `unit-tests` explicitly before CTest. For any C++ change, format with `clang-format -i` version 21 or newer (configured by `.clang-format`; older versions reformat correct code) and keep the build free of compiler warnings. Rely on CI for clang-tidy checks instead of running them locally; address any findings reported by CI.

Use `tests/fixtures/sample` or a throwaway vault for verification, not a personal vault. Add small regression fixtures for newly discovered Obsidian behaviors. Use the Qt-free vault implementation in `src/core/vault/`; the Qt app and duplicate Qt core have been retired. Keep vault operations in Vault and UI-independent state in Session, testable without a display. Put native host/bridge/platform code in `src/desktop/` and offline editor behavior in `src/frontend/`. Follow README's "Portable vault and recovery migration (#88, #89)" and "Source and test layout" for boundaries and tests; consult its "Boundaries and limitations" for current link behavior and save caveats.

See README for setup and manual smoke steps.

## Agent skills

### Issue tracker

Issues are tracked in GitHub Issues on `pcheng17/inkstow` via the `gh` CLI. See `docs/agents/issue-tracker.md`.

### Triage labels

Use `agent:ready`, `agent:running`, `agent:blocked`, and `agent:review` for Sandcastle issue lifecycle. See `docs/agents/triage-labels.md` for triage and recovery.

### Domain docs

Single-context: one `GLOSSARY.md` and `docs/adr/` at the repo root. See `docs/agents/domain.md`.
