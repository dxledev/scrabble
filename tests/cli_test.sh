#!/usr/bin/env bash
set -euo pipefail

binary="${1:?usage: cli_test.sh PATH_TO_SCRABBLE}"
root="$(mktemp -d /tmp/scrabble-cli-test.XXXXXX)"
trap 'rm -rf -- "$root"' EXIT
config="$root/config"
run() { QT_QPA_PLATFORM=offscreen "$binary" --config-dir "$config" "$@"; }
fail() { printf 'cli_test: %s\n' "$*" >&2; exit 1; }
expect() { grep -Fq -- "$2" <<<"$1" || fail "expected '$2' in: $1"; }

expect "$("$binary" --version)" "Scrabble 1.0.0"

help="$("$binary" --help)"
expect "$help" "theme preset"
if grep -Eiq 'caelestia|noctalia|quickshell|island|waybar|omarchy|hyprland|desktop shell' <<<"$help"; then
    fail "help text mentions a desktop shell"
fi

expect "$(run theme list)" "paper"
expect "$(run theme keys)" "double_word"
expect "$(run theme path)" "$config/theme.json"

before="$(run theme show)"
run theme preset paper --dry-run >/dev/null
[[ ! -e "$config/theme.json" ]] || fail "dry run wrote a file"
run theme preset paper >/dev/null
[[ -f "$config/theme.json" ]] || fail "preset did not save"
expect "$(run theme show)" '"name": "paper"'

run theme set 'accent=#c4b5fd' 'opacity=0.5' >/dev/null
shown="$(run theme show)"
expect "$shown" '"accent": "#c4b5fd"'
expect "$shown" '"background_alpha": 128'

if run theme set 'accent=nothex' 2>/dev/null; then fail "invalid color accepted"; fi
expect "$(run theme show)" '"accent": "#c4b5fd"'
if run theme set 'bogus=#ffffff' 2>/dev/null; then fail "unknown key accepted"; fi
if run theme preset nope 2>/dev/null; then fail "unknown preset accepted"; fi

cat > "$root/Colors.qml" <<'QML'
pragma Singleton
import QtQuick
Singleton {
    readonly property color background: "#e00e1e36"
    readonly property color backgroundAlt: "#0E1E36"
    readonly property color backgroundGray: "#2a3a56"
    readonly property color foreground: "#f4b999"
    readonly property color primary: "#9279aa"
    readonly property color border: "#9279aa"
    readonly property color danger: "#d2495b"
}
QML
run theme follow file "$root/Colors.qml" >/dev/null
shown="$(run theme show)"
expect "$shown" '"background": "#0e1e36"'
expect "$shown" '"background_alpha": 224'
expect "$shown" '"kind": "file"'
run theme detach >/dev/null
if grep -Fq '"source"' <<<"$(run theme show)"; then fail "detach left a source binding"; fi

sed -i 's/"#e00e1e36"/Qt.rgba(14 \/ 255, 30 \/ 255, 54 \/ 255, 0.88)/' "$root/Colors.qml"
run theme import "$root/Colors.qml" >/dev/null
shown="$(run theme show)"
expect "$shown" '"background": "#0e1e36"'
expect "$shown" '"background_alpha": 224'

run theme export "$root/exported.json" >/dev/null
[[ -f "$root/exported.json" ]] || fail "export did not write"
run theme import "$root/exported.json" >/dev/null

printf '%s\n' '{ not json' > "$config/theme.json"
if run theme show 2>/dev/null; then fail "corrupt theme file accepted"; fi
grep -Fq 'not json' "$config/theme.json" || fail "corrupt theme file was overwritten by a read command"
printf 'cli_test: ok\n'
