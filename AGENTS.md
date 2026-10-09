# Inkstow agent workflow

Make each change on a new branch in its own git worktree, and merge it through a pull request. Never commit or push to `main`.

For changes to the vault core or UI, run the CMake build and CTest commands in README's "Install and run" section before reporting success. On macOS, the `ui-smoke` test needs file system events (FSEvents); a sandbox that blocks them makes its outside-edit checks time out, so run it unsandboxed. These build the C++/Qt app and run core and offscreen UI tests against disposable fixtures. Format C++ changes with `clang-format -i` (configured by `.clang-format`), and keep the build free of compiler warnings.

Use `tests/fixtures/sample` or a throwaway vault for verification, not a personal vault. Add small regression fixtures for newly discovered Obsidian behaviors. The current core treats note content as opaque Markdown; link resolution and live preview are not implemented, and save conflict detection has an external-write race. Keep vault operations in `src/Vault.*` and UI-independent state in `src/Session.*`, testable without a display.

See README for setup, manual smoke steps, and current limitations.

## Agent skills

### Issue tracker

Issues are tracked in GitHub Issues on `pcheng17/inkstow` via the `gh` CLI. See `docs/agents/issue-tracker.md`.

### Triage labels

Uses the five default triage labels (`needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`). See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: one `GLOSSARY.md` and `docs/adr/` at the repo root. See `docs/agents/domain.md`.
