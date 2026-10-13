# Rust + Tauri migration preview

This is an **incomplete migration** alongside the existing Qt application. Do not use it with a valuable vault. The old app and its tests remain unchanged.

## Try the first slice

From this directory:

```sh
cargo test -p inkstow-core
PKG_CONFIG=/usr/bin/pkg-config cargo run -p inkstow-tauri
```

On Linux, Tauri 2 requires GTK 3, WebKitGTK 4.1, and their development headers. The host's idempotent `~/.vps-dotfiles/install --root` includes the prerequisites. On this host the Homebrew `pkg-config` appears first in PATH and does not find apt's `.pc` files, so use `PKG_CONFIG=/usr/bin/pkg-config` for Tauri builds. On other platforms use the Tauri 2 system prerequisites. Copy `tests/fixtures/sample` to a disposable directory, then paste its **absolute path** into the preview's vault field. The preview has no native folder picker yet.

## What works now

The Rust core discovers non-hidden Markdown notes and folders without traversing symlinks, validates vault-relative paths, opens UTF-8 notes with optional BOM, refuses switching while dirty, creates notes, and stages guarded saves with byte-for-byte conflict detection. It also detects outside changes and supports reload, guarded Keep mine, line-based Merge outside changes (including conflict markers), Save as new note, and Discard edits. Note actions now include system-trash deletion and rename/move into existing folders, with optional conservative wikilink rewriting. The minimal Tauri UI exposes these actions as well as opening a vault by path, listing, selecting, editing, creating, and saving notes. It polls the disk every 900 ms while visible and on focus, rather than using native file watchers; large vaults may be slow to rescan. These are new implementations, not a binding to Qt.

## Still to port before functional parity

- Native folder picker, last-vault persistence, native file/directory watching instead of polling.
- Broader parity tests for merge edge cases, undo of reload in the web editor, and efficient discovery in large vaults.
- Broader cross-platform tests of system trash and atomic no-replace renames; a move with link updates can still partially complete if a subsequent write fails. Link matching for Unicode case variants has not been verified against Qt.
- Wikilink navigation (follow link) and attachment opening; move-time link resolution is implemented internally but not yet exposed as a standalone command.
- Markdown highlighting, menus, shortcuts beyond Save, quit safeguards, and UI integration tests.
- Verify atomic replacement and permissions on each supported OS, especially Windows, and account for filesystem races as in the Qt implementation.

The core tests include the existing sample fixture and disposable vault tests. The C++ tests remain the reference behavior; expand Rust tests with their fixtures while porting each remaining feature. No release or bundle-size comparison is meaningful until the Tauri build runs on all release platforms.
