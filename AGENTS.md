# Inkstow agent workflow

Make each change on a new branch in its own git worktree, and merge it through a pull request. Never commit or push to `main`.

For changes to the vault core or UI, run the CMake build and CTest commands in README's "Install and run" section before reporting success. On macOS, the `ui-smoke` test needs file system events (FSEvents); a sandbox that blocks them makes its outside-edit checks time out, so run it unsandboxed. These build the C++/Qt app and run core and offscreen UI tests against disposable fixtures.

Use `tests/fixtures/sample` or a throwaway vault for verification, not a personal vault. Add small regression fixtures for newly discovered Obsidian behaviors. The current core treats note content as opaque Markdown; link resolution and live preview are not implemented, and save conflict detection has an external-write race. Keep vault operations in `src/Vault.*` and UI-independent state in `src/Session.*`, testable without a display.

See README for setup, manual smoke steps, and current limitations.
