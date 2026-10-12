#!/usr/bin/env bash
# Launch Inkstow on a private X11 display with a disposable fixture copy; run actions there.
set -euo pipefail

usage() {
    printf 'Usage: %s [--bin INKSTOW] [--size WxH] [--no-open] FIXTURE_DIR ACTIONS_SH\n' "$0" >&2
    exit 2
}

bin=build/debug/inkstow
size=1100x750
open=1
while (($#)); do
    case $1 in
    --bin) (($# >= 2)) || usage; bin=$2; shift 2 ;;
    --size) (($# >= 2)) || usage; size=$2; shift 2 ;;
    --no-open) open=0; shift ;;
    -*) usage ;;
    *) break ;;
    esac
done
(($# == 2)) || usage
[[ $size =~ ^[0-9]+x[0-9]+$ ]] || { printf 'Bad --size: %s\n' "$size" >&2; exit 2; }
for command in xvfb-run dbus-run-session xdotool ffmpeg; do
    command -v "$command" >/dev/null || { printf 'Missing tool: %s\n' "$command" >&2; exit 2; }
done
bin=$(realpath -m -- "$bin")
fixture=$(realpath -m -- "$1")
actions=$(realpath -m -- "$2")
[[ -x "$bin" && -d "$fixture" && -f "$actions" ]] || {
    printf 'Expected an Inkstow binary, fixture directory and actions script\n' >&2
    exit 2
}

trial=$(mktemp -d "${TMPDIR:-/tmp}/inkstow-ui.XXXXXX")
mkdir -p "$trial/home" "$trial/vault" "$trial/shots" "$trial/bin"
cp -a -- "$fixture/." "$trial/vault/"
# `shot NAME` saves the Inkstow window as $SHOTS/NAME.png.
cat >"$trial/bin/shot" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
(($# == 1)) || { printf 'Usage: shot NAME\n' >&2; exit 2; }
sleep "${SHOT_SETTLE:-0.3}"
ffmpeg -loglevel error -f x11grab -draw_mouse 0 -video_size "$SIZE" -i "$DISPLAY+0,0" \
    -frames:v 1 -y "$SHOTS/$1.png"
printf '%s\n' "$SHOTS/$1.png"
SH
chmod +x "$trial/bin/shot"
printf 'Trial: %s\nVault: %s/vault\nShots: %s/shots\n' "$trial" "$trial" "$trial"

TRIAL="$trial" ACTIONS="$actions" BIN="$bin" SIZE="$size" OPEN="$open" \
    xvfb-run -a -s "-screen 0 ${size}x24" dbus-run-session -- bash -c '
        set -euo pipefail
        export HOME="$TRIAL/home"
        export XDG_CONFIG_HOME="$HOME/.config"
        export XDG_CACHE_HOME="$HOME/.cache"
        export XDG_DATA_HOME="$HOME/.local/share"
        export VAULT="$TRIAL/vault" SHOTS="$TRIAL/shots" SIZE
        export PATH="$TRIAL/bin:$PATH"
        "$BIN" >"$TRIAL/inkstow.log" 2>&1 &
        app=$!
        trap '\''kill "$app" 2>/dev/null || :; wait "$app" 2>/dev/null || :'\'' EXIT
        WINDOW=
        for _ in $(seq 1 40); do
            WINDOW=$(xdotool search --onlyvisible --name "^Inkstow" 2>/dev/null | head -n 1) || :
            [[ -n $WINDOW ]] && break
            if ! kill -0 "$app" 2>/dev/null; then
                printf "Inkstow exited before opening a window; see %s/inkstow.log\n" "$TRIAL" >&2
                exit 1
            fi
            sleep 0.25
        done
        if [[ -z $WINDOW ]]; then
            printf "Inkstow window did not appear; see %s/inkstow.log\n" "$TRIAL" >&2
            exit 1
        fi
        export WINDOW
        xdotool windowmove --sync "$WINDOW" 0 0
        xdotool windowsize --sync "$WINDOW" "${SIZE%x*}" "${SIZE#*x}"
        xdotool windowfocus --sync "$WINDOW"
        if ((OPEN)); then
            # The vault path field has focus on launch.
            xdotool type --delay 5 "$VAULT"
            xdotool key Return
            sleep 0.5
        fi
        bash -e "$ACTIONS"
    '
