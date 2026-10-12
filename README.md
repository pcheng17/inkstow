# Inkstow

Prototype of a non-Electron Markdown vault editor built in C++17 and Qt 6 Widgets. Its standalone vault core lists and reads existing Markdown notes and guards saves against external changes. A disposable example vault lives at `tests/fixtures/sample`.

## Install and run

On Debian/Ubuntu Linux (macOS notes follow):

```sh
sudo ./scripts/install --system   # Qt 6 development libraries, CMake, and C++ compiler
cmake --preset debug      # or: release
cmake --build --preset debug --parallel
ctest --preset debug
./build/debug/inkstow     # run the desktop app on a graphical machine
```

On macOS, install Xcode or its Command Line Tools and [Homebrew](https://brew.sh), then run `./scripts/install --system` without `sudo` (it installs Homebrew's `cmake` and `qt`) and the same CMake build and CTest commands. CMake finds Homebrew's Qt through `brew --prefix qt`; pass `-DCMAKE_PREFIX_PATH=/path/to/qt` to use another Qt. If linking fails with `framework 'AGL' not found`, your Qt predates the macOS 26 SDK, which dropped AGL: run `brew upgrade qt` (6.9.0 fails; 6.11.2 works), then delete `build/` and reconfigure. Presets need CMake 3.21+; with an older CMake, use `cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug` and `ctest --test-dir build/debug --output-on-failure`.

On other systems, install CMake 3.20+, a C++17 compiler, and Qt 6.4+ Core and Widgets, then use the same CMake build and CTest commands. The application uses a light Qt palette even when the desktop's default is dark.

Enter an absolute path to a vault folder. To avoid altering the checked-in fixture, first copy it to a throwaway directory:

```sh
vault=$(mktemp -d)
cp -a tests/fixtures/sample/. "$vault/"
printf '%s\n' "$vault"
```

Open that path in Inkstow. Only `Welcome.md` and `Folder/Second note.md` should appear; dot-prefixed files and folders are excluded from discovery. Edit and save a note, then confirm the copied file changes on disk. Check that an unsaved note cannot be switched away from. Type `Folder/New note` under the note list and press **Create**: the copy should gain `Folder/New note.md`, selected and empty. Select a clean note and press **Rename / move**, enter a new vault-relative path in an existing folder, and confirm **Update links** (the default). Cancel changes nothing; **Don't update** explicitly warns links may break. Press **Delete**, then **Move to trash**: the note should move to the system trash and leave the list. With no unsaved edits, change the open note from another program: Inkstow should reload it and say so in the status bar. To test a conflict, edit a note in Inkstow without saving, modify the same file externally, then press Save: it should report a conflict, retain the unsaved editor text without overwriting the external version, and show a bar with **Reload from disk** (discard your edits), **Keep mine** (replace the external version), and **Merge outside changes** (merge into the editor without saving). Overlapping edits get editable conflict markers; review and Save explicitly. To try wikilinks, open a disposable copy of `tests/fixtures/links`, place the cursor inside `[[Twin]]` in `A/Source.md`, and press **Follow link**. The expected targets and unresolved Obsidian-compatibility questions are in that fixture's `README.txt`. Unsaved edits block following a link to another note or opening an attachment.

## Pre-commit hook

The `.githooks/pre-commit` hook auto-formats staged `.h` and `.cpp` files with `clang-format` before each commit. Install `clang-format` and activate the hook once after cloning:

```sh
git config core.hooksPath .githooks
```

## Headless tests

The build and test commands above build the C++ core, CLI, and Qt Widgets app, then run filesystem and session tests plus an offscreen UI smoke test. The tests also check note discovery against the fixture vault. No graphical display is required. Run `./build/debug/vault-inspect tests/fixtures/sample` to inspect the fixture, or pass another throwaway vault path without opening a window.

## Boundaries and limitations

This is an early prototype, **not** a safe replacement for Obsidian on valuable vaults. It can rename/move individual notes but cannot rename folders or render Live Preview. Link updates preflight all discoverable Markdown notes, refuse complex relevant wikilinks (aliases, fragments, embeds), invalid UTF-8, and unresolved relevant links; they rewrite simple affected links to root-qualified paths without `.md`, rather than copying Obsidian's shortest-link serialization. If an extensionless file would shadow a rewritten link, or a basename already ends in `.md`, link updates refuse the move before changing any files. Names containing wikilink delimiters (`#`, `|`, `[`, `]`, or newlines) are also refused for link updates. Case-only renames are not supported on case-insensitive filesystems. UTF-8 BOMs are preserved. Choosing not to update links may break them. The move and rewrites are not a filesystem transaction: later write failures may leave a moved note and partially updated links (the error reports this). External writes can race preflight and per-file replacement. **Follow link** resolves Markdown notes and opens attachments in the system's default app, but its target ordering and case handling are provisional and have not been checked against a running Obsidian. Heading fragments select the file, not a position within it; block references and embeds are not supported. It watches the open note: outside changes reload a clean editor, while unsaved edits are kept and a bar offers to reload from disk or keep your version. **Keep mine** only replaces the outside version that was reported, and reloading can be undone. **Merge outside changes** combines disjoint line edits and marks overlapping edits, including deletion versus change. Very large notes are refused by the line merge rather than exhausting memory. The merged editor text remains unsaved and Save still refuses a newer outside change. If a note is deleted outside Inkstow while you have unsaved edits, the bar offers **Save as new note** to recreate the selected path without replacing a reappeared file, or **Discard edits**. The note list refreshes when watched vault folders change or when the window is reactivated. Large vaults may exceed the OS limit on watched paths, so some changes may not appear until reactivation. New notes need a vault-relative path whose folders and name don't start with `.`; `.md` is added when missing, and an existing file is never replaced. Delete only moves notes to the system trash; where there is no trash, it refuses rather than deleting permanently. Saves replace the file using `QSaveFile`. Replacement can change inode and metadata; hard links are not preserved. Markdown must be valid UTF-8 to open in the editor. The vault core rejects path traversal and never reads or writes a note through a symlink, but filesystem races have not been hardened. Use only disposable copies for now.

### Save conflict guarantee

Inkstow compares a note's disk bytes with the last loaded or saved bytes before saving, then compares again just before committing the staged replacement. If either comparison finds different bytes, the save is refused and your unsaved edits stay in the editor. This is best-effort conflict detection, **not** a guarantee that outside changes cannot be lost: another program (including another Inkstow instance) can write after the final comparison and before the replacement, and its write may be overwritten. Atomic replacement keeps Inkstow from leaving a partially written note, but does not make the comparison and replacement atomic together. Inkstow does not coordinate saves with other processes or guarantee recovery of a write lost in that window. Use independent backups for valuable vaults.

Filesystem operations live in `src/Vault.*` and UI-independent session state in `src/Session.*`. The Qt Widgets window is in `src/Window.*`. AppImage static research is in `docs/obsidian-appimage-inspection.md`. Verified link and rename trials with disposable Obsidian vaults are in `docs/obsidian-runtime-observations.md`; use the project-local `/skill:obsidian-behavior` (at `.agents/skills/obsidian-behavior/SKILL.md`) to investigate further. To launch Inkstow on a headless display, drive it, and capture screenshots, use `scripts/inkstow-headless.sh` as described in `.agents/skills/inkstow-ui/SKILL.md`. Project skills are also exposed to Claude Code through the `.claude/skills` symlink.
