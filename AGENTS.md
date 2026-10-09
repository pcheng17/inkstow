# Inkstow agent workflow

Temporary repository rule: Do all work directly on `main` in this repository directory. Do not create or use feature branches or additional worktrees.

For changes to the vault core or UI, run the CMake build and CTest commands in README's "Install and run" section before reporting success. On macOS, the `ui-smoke` test needs file system events (FSEvents); a sandbox that blocks them makes its outside-edit checks time out, so run it unsandboxed. These build the C++/Qt app and run core and offscreen UI tests against disposable fixtures.

Use `tests/fixtures/sample` or a throwaway vault for verification, not a personal vault. Add small regression fixtures for newly discovered Obsidian behaviors. The current core treats note content as opaque Markdown; link resolution and live preview are not implemented, and save conflict detection has an external-write race. Keep vault operations in `cpp/vault.*` and UI-independent state in `cpp/session.*`, testable without a display.

See README for setup, manual smoke steps, and current limitations.
