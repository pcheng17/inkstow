# Inkstow agent workflow

Temporary repository rule: Do all work directly on `main` in this repository directory. Do not create or use feature branches or additional worktrees.

For changes to the vault core or UI, run `./verify` before reporting success. This is the headless gate: TypeScript tests and build, Rust formatting, lint, Rust tests, and a fixture-vault smoke check. For Tauri command/config changes also run `./verify --native` on a machine with the Linux system dependencies (see README); report explicitly when this gate was unavailable.

Use `tests/fixtures/sample` or a throwaway vault for verification, not a personal vault. Add small regression fixtures for newly discovered Obsidian behaviors. The current core treats note content as opaque Markdown; link resolution, live preview, and concurrent external edits are not implemented. Keep vault operations in `crates/vault-core`, testable without a display, and pass only small typed commands across the Tauri boundary.

See README for setup, manual smoke steps, and current limitations.
