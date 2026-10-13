#!/usr/bin/env bash
# Headless smoke of the Tauri preview's quit protection. Each scenario launches the app on a
# private X display with a disposable copy of tests/fixtures/sample and closes the window the
# way a window manager's close button does. Run from the repo root after
# `PKG_CONFIG=/usr/bin/pkg-config cargo build -p inkstow-tauri` in rust/.
set -euo pipefail

bin=${1:-rust/target/debug/inkstow-tauri}
here=$(cd "$(dirname "$0")" && pwd)
actions=$(mktemp -d "${TMPDIR:-/tmp}/inkstow-quit-smoke.XXXXXX")
export WM_CLOSE="$here/wm-close.py"

# Shared helpers, prepended to each scenario. The window sits at 0,0 at 1100x750.
cat >"$actions/lib.sh" <<'SH'
fail() { printf 'FAIL: %s\n' "$*" >&2; shot failure; exit 1; }
alive() { xdotool search --name '^Inkstow' >/dev/null 2>&1; }
request_close() { python3 -I "$WM_CLOSE" "$WINDOW"; sleep 0.8; }
expect_alive() { alive || fail "window closed: $1"; }
expect_exit() {
    for _ in $(seq 1 20); do alive || return 0; sleep 0.25; done
    fail "window still open: $1"
}
expect_bytes() { cmp -s "$1" "$2" || fail "$3: $(cat "$2" 2>/dev/null)"; }
sleep 3
xdotool mousemove 500 33 click 1
xdotool type "$VAULT"
xdotool mousemove 954 33 click 1
sleep 1
xdotool mousemove 70 215 click 1   # Welcome.md
sleep 1
note="$VAULT/Welcome.md"
original=$(mktemp)
cp "$note" "$original"
type_edit() {
    xdotool mousemove 680 600 click 1
    xdotool key ctrl+End
    xdotool type --delay 30 ' smoke edit'
    sleep 1
}
# A dirty close must leave the window open behind the prompt.
request_prompt() { request_close; expect_alive "dirty close must prompt"; }
SH

scenario() {
    [[ -z ${ONLY:-} || $ONLY == "$1" ]] || { cat >/dev/null; return 0; }
    local name=$1
    { cat "$actions/lib.sh"; cat; printf 'printf "PASS: %s\\n" %q\n' "$name"; } >"$actions/$name.sh"
    printf '== %s\n' "$name"
    scripts/inkstow-headless.sh --bin "$bin" --no-open tests/fixtures/sample "$actions/$name.sh" \
        >"$actions/$name.log" 2>&1 || { cat "$actions/$name.log"; exit 1; }
    grep -E '^(PASS|Shots):' "$actions/$name.log"
}

scenario clean-close <<'SH'
request_close
expect_exit "clean close should not prompt"
expect_bytes "$original" "$note" "clean close changed the note"
SH

scenario cancel-then-discard <<'SH'
type_edit
request_prompt
request_close   # A repeated close must not stack a second prompt.
shot prompt
xdotool key Return   # Cancel has focus by default.
sleep 0.5
shot cancelled
expect_alive "cancel must keep the window"
expect_bytes "$original" "$note" "cancel wrote the note"
request_prompt
shot prompt-again
xdotool key Tab Return   # Discard and quit
expect_exit "discard should quit"
expect_bytes "$original" "$note" "discard changed the note"
SH

scenario escape-cancels <<'SH'
type_edit
request_prompt
xdotool key Escape
sleep 0.5
expect_alive "escape must cancel"
request_prompt
shot prompt-after-escape
xdotool key Tab Tab Return   # Save and quit
expect_exit "save should quit"
grep -q 'smoke edit' "$note" || fail "save did not write the edit"
SH

scenario save-and-quit <<'SH'
type_edit
request_prompt
xdotool key Tab Tab Return   # Save and quit
expect_exit "save should quit"
grep -q 'smoke edit' "$note" || fail "save did not write the edit"
SH

scenario save-conflict-stays <<'SH'
type_edit
printf 'outside\n' >"$note"
request_prompt
xdotool key Tab Tab Return   # Save and quit
sleep 1
shot conflict
expect_alive "a conflicting save must keep the window"
[[ $(cat "$note") == outside ]] || fail "conflicting save overwrote the outside change"
SH

scenario save-missing-stays <<'SH'
type_edit
rm "$note"
request_prompt
xdotool key Tab Tab Return   # Save and quit
sleep 1
shot missing
expect_alive "saving a missing note must keep the window"
[[ ! -e $note ]] || fail "saving a missing note recreated it"
SH

printf 'All quit smoke scenarios passed. Logs: %s\n' "$actions"
