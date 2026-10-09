# Inkstow

Prototype of a non-Electron Markdown vault editor built in C++17 and Qt 6 Widgets. Its standalone vault core lists and reads existing Markdown notes and guards saves against external changes. A disposable example vault lives at `tests/fixtures/sample`.

## Install and run

On Debian/Ubuntu Linux:

```sh
sudo ./install --system   # Qt 6 development libraries, CMake, and C++ compiler
./verify                  # configure, build, run core and offscreen UI tests
./build/inkstow           # run the desktop app on a graphical machine
```

On other systems, install CMake 3.20+, a C++17 compiler, and Qt 6.4+ Core and Widgets, then run `./verify`. The application uses a light Qt palette even when the desktop's default is dark.

Enter an absolute path to a vault folder. To avoid altering the checked-in fixture, first copy it to a throwaway directory:

```sh
vault=$(mktemp -d)
cp -a tests/fixtures/sample/. "$vault/"
printf '%s\n' "$vault"
```

Open that path in Inkstow. Only `Welcome.md` and `Folder/Second note.md` should appear; dot-prefixed files and folders are excluded from discovery. Edit and save a note, then confirm the copied file changes on disk. Check that an unsaved note cannot be switched away from. To test a conflict, edit a note in Inkstow without saving, modify the same file externally, then press Save: it should report a conflict and retain the unsaved editor text without overwriting the external version.

## Headless verification

`./verify` builds the C++ core, CLI, and Qt Widgets app, runs filesystem and session tests plus an offscreen UI smoke test, and checks CLI note discovery against the fixture vault. No graphical display is required. `build/vault-inspect /path/to/vault` inspects any vault without opening a window.

## Boundaries and limitations

This is an early prototype, **not** a safe replacement for Obsidian on valuable vaults. It does not create notes, resolve links, render Live Preview, or proactively detect external edits. Saves compare disk bytes against the last loaded/saved bytes and atomically replace the file using `QSaveFile`; conflicts retain editor text. An external program can still write between the final comparison and replacement. Replacement can change inode and metadata; hard links are not preserved. Markdown must be valid UTF-8 to open in the editor. The vault core rejects path traversal and out-of-vault symlinks, but filesystem races have not been hardened. Use only disposable copies for now.

Filesystem operations live in `cpp/vault.*` and UI-independent session state in `cpp/session.*`. The Qt Widgets window is in `cpp/window.*`. AppImage research notes are in `docs/obsidian-appimage-inspection.md`; its static findings are hypotheses until verified against a disposable Obsidian vault.
