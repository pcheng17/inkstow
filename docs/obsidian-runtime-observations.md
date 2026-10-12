# Obsidian runtime observations: wikilinks and renames

These are **observations of Obsidian 1.14.4 on Linux**, not general rules for every version, platform, setting, or ambiguous tie. The AppImage SHA-256 was `6362ddbeeeebb7bbccb48fae009572cf2284ef92f5c919c1332aba48de6ffeaa`; its startup log said it loaded the bundled `resources/obsidian.asar`. Tests ran under Xvfb with isolated temporary homes and disposable vaults; some trials reused a temporary profile after restoring the vault's starting files. The vault's `.obsidian/app.json` was `{}` in the trials checked; the link-format setting was not explicitly changed. Renames/moves used Obsidian's own file explorer, not an external filesystem command. Inspect the bytes of the source `.md` files after each action: the editor's rendered link text alone does not establish what was written.

`tests/fixtures/obsidian-rename/{unique,duplicate}` supplies small starting vaults for repeating the cases. Copy a fixture to a temporary folder before opening it; each test starts from a fresh copy. The [obsidian-behavior skill](../.agents/skills/obsidian-behavior/SKILL.md) gives the isolated launch and evidence procedure. Earlier [AppImage inspection](obsidian-appimage-inspection.md) inferred possible resolution/rewrite behavior from bundled JavaScript; the cases below are separately verified runtime observations.

## One target

Starting from `tests/fixtures/obsidian-rename/simple`: `Source.md` is exactly `See [[Target]].\n`; `Target.md` is exactly `# Target\nSome target content.\n` (the `\n` notation here represents line breaks).

| Action in Obsidian | Prompt or choice | On-disk result |
| --- | --- | --- |
| Rename `Target.md` to `Renamed.md` | "Update links": 1 link in 1 file; **Just once** | `Source.md`: `See [[Renamed]].` |
| Same rename from a fresh starting vault | **Do not update** | `Source.md`: `See [[Target]].`; the target file is nevertheless renamed. |

The file move/rename occurred before the prompt was answered. The prompt also offered **Always update**; that choice was not tested. Dismissing a prompt by ending the app is not equivalent to selecting **Do not update**.

## One target in a subdirectory

Starting files: `SourceBare.md` contains `Bare: [[Target]]`; `SourceQualified.md` contains `Qualified: [[subdir/Target]]`; the only target is `subdir/Target.md`. For each row, choose **Just once** on a fresh vault.

| Action in Obsidian | Prompt count | `SourceBare.md` after | `SourceQualified.md` after |
| --- | --- | --- | --- |
| Rename `subdir/Target.md` to `subdir/NewTarget.md` | 2 links in 2 files | `Bare: [[NewTarget]]` | `Qualified: [[NewTarget]]` |
| Rename folder `subdir` to `renamed` | 1 link in 1 file | `Bare: [[Target]]` (unchanged) | `Qualified: [[Target]]` |

The rewritten qualified link became a bare link here. That does **not** mean Obsidian always removes directory names: there was only one file with that basename. The folder-rename prompt used the generic wording "links that link to this file" even though the renamed item was a folder.

## Duplicate targets in different folders

Starting files: `A/Target.md` and `B/Target.md` have different content. `RootSource.md`, `A/Source.md`, and `B/Source.md` each contain `[[Target]]`.

Following the link in Obsidian opened `A/Target.md` from `A/Source.md` and `B/Target.md` from `B/Source.md`. From `RootSource.md` it opened `A/Target.md` in this fixture. The root result is a **tie observation, not a guaranteed tie-break rule**; don't rely on it to specify compatibility. The distinct headings in the target notes and the breadcrumb path confirmed the destination.

| Action in Obsidian | Prompt count and choice | Root source after | A source after | B source after |
| --- | --- | --- | --- | --- |
| Rename `A/Target.md` to `A/NewTarget.md` | 2 links in 2 files, **Just once** | `[[NewTarget]]` | `[[NewTarget]]` | `[[Target]]` |
| Move `A/Target.md` to the existing empty folder `C/` using **Move file to...** (same basename) | 3 links in 3 files, **Just once** | `[[C/Target]]` | `[[C/Target]]` | `[[B/Target]]` |

The second result is particularly important: Obsidian also qualified `B/Source.md`'s link, although that link pointed to the *other* target. This kept the intended targets distinguishable after the move. The observed rewrite depends on the original link destinations and the available candidates after the move, not just on textual replacement of `A` with `C`.

## Headless trial setup and Inkstow comparison

`Xvfb` and `xdotool` worked for real GUI interaction; `ffmpeg` captured X11 screenshots. Obsidian's **Open folder as vault** initially appeared to do nothing: the existing user D-Bus session started `xdg-desktop-portal-gtk` without `DISPLAY` (`cannot open display` in the user service journal), leaving the portal unresponsive. Starting `dbus-run-session` *inside* `xvfb-run` made the folder picker appear and allowed a disposable vault to open. The reusable skill/helper instead seeds an isolated profile's vault list, which avoids fragile chooser coordinates. The source AppImage lacked its executable bit; the helper copies it and sets that bit on the disposable copy.

Inkstow was also built and driven with `xdotool` under Xvfb against a copy of `tests/fixtures/sample`. Opening the vault, editing `Welcome.md` and clicking **Save** wrote the expected text on disk. That was a GUI smoke trial, not a replacement for the existing filesystem-backed `tests/CoreTests.cpp` save/conflict tests or the Qt offscreen `tests/UiTests.cpp` save-button test (`ctest --preset debug`). Keep detailed correctness cases at those seams; use real-display trials to catch window, focus, input, and end-to-end workflow failures.

## Scope and next questions

These cases used bare and simple path-qualified wikilinks, no aliases, fragments, embeds, case variants, or multiple equal-depth candidates in the same source folder. `[[Target]]` in a root note with tied candidates needs more tests before specifying a stable ordering. Repeat the matrix with other **New link format** settings and **Always update internal links** settings before claiming a general serialization algorithm. Keep Obsidian's behavior as a compatibility reference: Inkstow may deliberately choose safer or narrower behavior, which should be stated explicitly rather than silently assumed equivalent.
