---
name: inkstow-ui
description: Launch Inkstow on a headless X11 display, drive it with clicks and keys, and capture screenshots. Use when changing how the Inkstow UI looks or behaves, when asked to run, show, or screenshot the app, or to check an end-to-end workflow in the real window.
---

# Drive and screenshot the Inkstow UI

Screenshots show what a change looks like; they do not prove it is correct. Keep behavior checks in `tests/UiTests.cpp` and `tests/CoreTests.cpp`, and still run `ctest --preset debug` per `AGENTS.md`.

1. **Build.** `cmake --build --preset debug --parallel` in the worktree you are changing. For a before/after comparison, also build the base commit in its own worktree (`git worktree add ../inkstow-base main`, then configure and build there). Completion: each binary you will compare exists and reflects its commit.
2. **Write an actions script.** The helper runs it with `bash -e` (any shebang is ignored, and the first failing command fails the run) from the directory you launched the helper in. It runs once Inkstow is visible, with the fixture copy already opened. Available: `$VAULT` (disposable vault copy), `$TRIAL`, `$SHOTS`, `$WINDOW` (X window id), `$SIZE`, `xdotool`, and `shot NAME`, which saves the window as `$SHOTS/NAME.png` with the cursor hidden. The window sits at screen `0,0`, so screen coordinates are window coordinates. Prefer keyboard input (`xdotool type`, `xdotool key Tab Return`) where focus is predictable. For clicks, read coordinates off a fresh screenshot of the *same build*, because a redesign moves widgets. Completion: every state you want to show has a `shot` after the action that reaches it.
3. **Run.** From the repo root: `scripts/inkstow-headless.sh [--bin PATH] [--size WxH] [--no-open] FIXTURE_DIR ACTIONS_SH`. The defaults are `build/debug/inkstow`, `1100x750` (the app's default size), and a typed-in vault path followed by Return. Use `--no-open` to show the empty start screen. Use `tests/fixtures/sample` or another fixture, never a personal vault; the helper works on a copy in a temp directory. Run the same actions script with the same size and fixture for both builds. Completion: the helper prints the trial directory and exits 0; on failure, read `$TRIAL/inkstow.log`.
4. **Look and report.** Open the PNGs and check them yourself before you describe them. Send before/after pairs to the user (with SendUserFile, if available) when they are judging a design. Leave screenshots in `/tmp` or the scratchpad; commit them only if the user asks. Completion: the user has the images, or a clear statement of what you saw.

## Example

```sh
actions=$(mktemp)
cat > "$actions" <<'SH'
shot opened
xdotool mousemove 50 59 click 1   # Welcome.md in the note list
shot note
SH
scripts/inkstow-headless.sh tests/fixtures/sample "$actions"
```

## Caveats

- Xvfb runs without a window manager: windows and dialogs have **no title bars or borders**, and dialogs appear wherever Qt places them. Judge the app's contents, not window chrome.
- Fonts and rendering follow this server's installed fonts and Qt style, which may differ from a desktop or from macOS.
- The X screen is exactly `$SIZE`, the same as the window, so Qt keeps menus and dialogs on-screen and `shot` captures them. A dropdown near an edge may open in a different direction than it would on a larger desktop.
- Use `SHOT_SETTLE=1 shot NAME` if an animation or a slow vault scan hasn't finished when the shot is taken.

The helper requires `xvfb-run`, `dbus-run-session`, `xdotool`, and `ffmpeg`. If any is missing, add it idempotently to `~/.vps-dotfiles/install` instead of installing it ad hoc.
