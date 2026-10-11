---
name: obsidian-behavior
description: Investigate or compare Obsidian vault, link, rename, or editor behavior against Inkstow by running the real Obsidian GUI in an isolated headless display. Use when a compatibility question needs runtime evidence instead of static AppImage inference.
---

# Observe Obsidian behavior

Use a **differential fixture**: preserve exact inputs and compare the resulting files, not just screenshots. Read [verified observations](../../../docs/obsidian-runtime-observations.md) for already-tested cases and limitations; [static inspection](../../../docs/obsidian-appimage-inspection.md) is only a source of hypotheses.

1. **Bound the question.** Name the Obsidian version, a single action, the relevant settings, and the exact file bytes to inspect. Start from one unambiguous throwaway case before adding duplicates or variants. For each independent case, start with a fresh fixture copy. Completion: the before-state and an observable after-state are both written down.
2. **Launch an isolated GUI.** Use `scripts/obsidian-headless.sh APPIMAGE FIXTURE_DIR ACTIONS_SH` from the repo root. It copies the fixture and AppImage into `/tmp`, seeds an isolated Obsidian profile pointing to that copy, and starts Xvfb plus a private D-Bus session so the GTK folder picker works. The actions script runs while Obsidian and `$DISPLAY` are alive, with `$VAULT` and `$TRIAL` set. Obsidian may check for updates; inspect `$TRIAL/obsidian.log` for the **loaded app package**, not just the AppImage filename. Use an explicit, trusted AppImage path; the binary is not checked into this repo. Completion: the visible Obsidian window is open on the temporary vault, with no personal vault involved.
3. **Act and capture.** Drive Obsidian's UI with `xdotool` (rename/move *inside the app*, not with `mv` while testing rename behavior). Save a screenshot before answering any prompt and after answering it. `ffmpeg -f x11grab -video_size 1280x900 -i "$DISPLAY" -frames:v 1 -y "$TRIAL/prompt.png"` captures a frame if ffmpeg is installed. Record whether you chose **Just once**, **Always update**, or **Do not update**. A file move can happen *before* the confirmation; exiting at the prompt is not a choice. Completion: the action and any prompt/count/choice are recorded, and on-disk note bytes are captured after the app responds.
4. **Check and publish.** Compare the copied vault's Markdown bytes and paths with the before-state; follow ambiguous links and verify destination content or breadcrumb, not just the displayed title. Separate observed behavior from hypotheses, and qualify any tie-breaking claim. Add small manual regression fixtures under `tests/fixtures/` for new behavior. Document the version, settings, initial tree, UI steps, prompt, choice, final tree and file bytes in `docs/obsidian-runtime-observations.md` (on a branch/worktree per `AGENTS.md`). Completion: another person can recreate the case from the committed fixture and instructions without any `/tmp` artifacts.

## Minimal action script

Create a throwaway script, then run the helper with a **fixture directory**, never a personal vault:

```sh
APPIMAGE=/path/to/Obsidian.AppImage
actions=$(mktemp)
cat > "$actions" <<'SH'
sleep 3 # allow the vault index to settle
xdotool search --onlyvisible --name Obsidian getwindowname
# Add measured mouse/key actions here; capture the prompt before clicking it.
find "$VAULT" -type f -name '*.md' -print
SH
scripts/obsidian-headless.sh "$APPIMAGE" tests/fixtures/obsidian-rename/unique "$actions"
```

The helper prints the trial directory and leaves it for inspection. Quote paths; fixture contents and app logs must remain disposable. `ffmpeg` is optional for the helper itself but useful for screenshot evidence. The helper requires `xvfb-run`, `dbus-run-session`, `xdotool`, and Python 3. If any required tool is unavailable on this server, update `~/.vps-dotfiles/install` idempotently rather than installing a new dependency ad hoc.
