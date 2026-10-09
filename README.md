# Inkstow

Prototype of a non-Electron Markdown vault editor: Tauri 2, TypeScript, CodeMirror 6, and a standalone Rust vault library. Current scope is opening a folder, listing its Markdown notes, and editing/saving existing notes. A disposable example vault lives at `tests/fixtures/sample`.

## Install

On Debian/Ubuntu Linux:

```sh
sudo ./install --system    # GTK/WebKit/Tauri development libraries; requires sudo
./install --user           # rustup toolchain and npm ci
```

The script is idempotent and local to this project. Node.js/npm must already be available. On other Linux distributions, install the [Tauri 2 system prerequisites](https://v2.tauri.app/start/prerequisites/#linux) with your package manager, then run `./install --user`. For shells that do not yet see Rust, run `source "$HOME/.cargo/env"` or add `~/.cargo/bin` to PATH. Tauri CLI is installed locally as `@tauri-apps/cli` by npm, not globally.

## Headless verification

```sh
./verify                # no display, GTK, or WebKit required
./verify --native       # additionally compile the Tauri backend; requires --system
```

`./verify` runs the frontend tests and production build, Rust formatting/lint/tests (including a disposable fixture-vault save/conflict smoke test), and a CLI listing check against the fixture vault. The session test checks that a failed save retains editor text; the Rust smoke test checks the actual file on disk. These headless tests do not launch the Tauri window. `--native` selects the system `pkg-config` when Homebrew's version cannot find Ubuntu's GTK metadata. Run the commands in CI on a headless runner; add a Linux runner with `--system` for the native compile gate. The CLI can inspect any vault without launching a window: `cargo run -p vault-core --bin vault-inspect -- /path/to/vault`.

To run the desktop app on a graphical machine: `PKG_CONFIG=/usr/bin/pkg-config npm run tauri dev` on this Ubuntu machine (Homebrew's `pkg-config` shadows the system one); on other platforms use `npm run tauri dev`. Enter an absolute vault folder path; select a note, edit, and click Save. To avoid altering the checked-in fixture, first copy `tests/fixtures/sample` to a temporary directory. Verify that the note changes on disk; verify that an unsaved note cannot be switched away from. To test a conflict, open a note, edit it in Inkstow without saving, then edit the same file externally. Save in Inkstow: it should report a conflict and retain your unsaved editor text without overwriting the external change.

## Boundaries and limitations

This is an early prototype, **not** a safe replacement for Obsidian on valuable vaults. It does not create notes, resolve links, render Live Preview, or proactively detect external edits. Saves compare the current disk content against the last loaded/saved content and atomically replace the file via a same-directory temporary file; a conflict leaves editor text intact. This is not a transaction against other programs: an external writer can still race between the final comparison and replacement. Replacing the file changes its inode and may break hard links; only file permissions are preserved, not all metadata. CodeMirror currently edits plain Markdown with syntax highlighting; wiki links remain text. The Rust core rejects path traversal and out-of-vault symlinks and skips dot-prefixed files and folders during note discovery (including `.obsidian`), but filesystem races have not been hardened. Use only disposable copies for now. Native desktop compilation needs GTK/WebKit headers; headless verification deliberately does not.

The Rust library has no Tauri dependency; put parsing, indexing, and file-format compatibility tests there. `src/session.ts` owns UI state and can be tested with a fake backend. Keep UI-to-Rust access behind `src/api.ts`. This makes it possible to verify most changes without ever launching a desktop window.
