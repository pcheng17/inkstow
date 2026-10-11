#!/usr/bin/env bash
# Launch a disposable copy of a fixture in Obsidian; run actions on the same X11 display.
set -euo pipefail

if (($# != 3)); then
    printf 'Usage: %s APPIMAGE FIXTURE_DIR ACTIONS_SH\n' "$0" >&2
    exit 2
fi
for command in xvfb-run dbus-run-session xdotool python3; do
    command -v "$command" >/dev/null || { printf 'Missing tool: %s\n' "$command" >&2; exit 2; }
done
appimage=$(realpath -- "$1")
fixture=$(realpath -- "$2")
actions=$(realpath -- "$3")
[[ -f "$appimage" && -d "$fixture" && -f "$actions" ]] || {
    printf 'Expected an AppImage file, fixture directory and actions script\n' >&2
    exit 2
}

trial=$(mktemp -d "${TMPDIR:-/tmp}/inkstow-obsidian.XXXXXX")
mkdir -p "$trial/home/.config/obsidian" "$trial/vault"
cp -a -- "$fixture/." "$trial/vault/"
cp -- "$appimage" "$trial/Obsidian.AppImage"
chmod u+x "$trial/Obsidian.AppImage"
python3 - "$trial" <<'PY'
import json
import pathlib
import secrets
import sys
import time

trial = pathlib.Path(sys.argv[1])
config = {"vaults": {secrets.token_hex(8): {
    "path": str(trial / "vault"), "ts": int(time.time() * 1000), "open": True,
}}}
(trial / "home/.config/obsidian/obsidian.json").write_text(json.dumps(config))
PY
printf 'Trial: %s\nVault: %s/vault\n' "$trial" "$trial"

TRIAL="$trial" ACTIONS="$actions" xvfb-run -a -s '-screen 0 1280x900x24' \
    dbus-run-session -- bash -c '
        set -euo pipefail
        export HOME="$TRIAL/home"
        export XDG_CONFIG_HOME="$HOME/.config"
        export XDG_CACHE_HOME="$HOME/.cache"
        export XDG_DATA_HOME="$HOME/.local/share"
        export VAULT="$TRIAL/vault"
        export APPIMAGE_EXTRACT_AND_RUN=1
        "$TRIAL/Obsidian.AppImage" --no-sandbox --disable-gpu >"$TRIAL/obsidian.log" 2>&1 &
        app=$!
        trap '\''kill "$app" 2>/dev/null || :; wait "$app" 2>/dev/null || :'\'' EXIT
        visible=0
        for _ in $(seq 1 40); do
            if xdotool search --onlyvisible --name Obsidian >/dev/null 2>&1; then
                visible=1
                break
            fi
            if ! kill -0 "$app" 2>/dev/null; then
                printf "Obsidian exited before opening a window; see %s/obsidian.log\n" "$TRIAL" >&2
                exit 1
            fi
            sleep 1
        done
        if ((visible == 0)); then
            printf "Obsidian window did not appear; see %s/obsidian.log\n" "$TRIAL" >&2
            exit 1
        fi
        bash "$ACTIONS"
    '
