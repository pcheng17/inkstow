# Inkstow agent workflow

Temporary repository rule: Do all work directly on `main` in this repository directory. Do not create or use feature branches or additional worktrees.

For changes to the vault core or UI, run `./verify` before reporting success. This builds the C++/Qt app, runs headless core and offscreen UI tests, and checks fixture-vault discovery.

Use `tests/fixtures/sample` or a throwaway vault for verification, not a personal vault. Add small regression fixtures for newly discovered Obsidian behaviors. The current core treats note content as opaque Markdown; link resolution and live preview are not implemented, and save conflict detection has an external-write race. Keep vault operations in `cpp/vault.*` and UI-independent state in `cpp/session.*`, testable without a display.

See README for setup, manual smoke steps, and current limitations.
