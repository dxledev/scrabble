#!/usr/bin/env bash
set -euo pipefail

script_directory="$(cd -- "${BASH_SOURCE[0]%/*}" && pwd)"
project_directory="${script_directory%/tests}"
test_root="$(/usr/bin/mktemp -d /tmp/scrabble-install-test.XXXXXX)"

fail() {
    printf 'install_test: %s\n' "$*" >&2
    exit 1
}

assert_file() {
    [[ -f "$1" ]] || fail "Expected regular file: $1"
}

assert_contains() {
    /usr/bin/rg -Fq -- "$2" "$1" || fail "Expected '$2' in $1"
}

assert_equals() {
    [[ "$1" == "$2" ]] || fail "Expected '$2', got '$1'"
}

assert_backup_contains() {
    local backup
    backup="$(compgen -G "$1" | /usr/bin/head -n 1)"
    [[ -n "$backup" ]] || fail "Expected backup matching $1"
    assert_contains "$backup" "$2"
}

build_directory="$test_root/build"
prefix="$test_root/prefix"
config_directory="$test_root/config/scrabble"
apps_list="$test_root/config/apps.list"
selector="$test_root/themes/.caelestia-use"
selector_target="$test_root/theme-source"
log="$test_root/cli.log"
/usr/bin/mkdir -p -- "$build_directory" "${apps_list%/*}" "${selector%/*}" "$selector_target"
cat > "$build_directory/scrabble" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' "$*" >> "$TEST_LOG"
arguments="$*"
config_directory=
while (( $# )); do
    if [[ "$1" == --config-dir ]]; then
        config_directory="$2"
        shift 2
    else
        shift
    fi
done
if [[ " $arguments " == *" --dry-run "* ]]; then
    exit 0
fi
/usr/bin/mkdir -p -- "$config_directory"
printf '%s\n' '{}' > "$config_directory/theme.json"
EOF
/usr/bin/chmod 755 -- "$build_directory/scrabble"
printf '%s\n' 'other.desktop|/icons/other.svg|Other' > "$apps_list"
/usr/bin/ln -s -- "$selector_target" "$selector"
export TEST_LOG="$log"

"$project_directory/scripts/install-desktop.sh" --dry-run --build-dir "$build_directory" \
    --prefix "$prefix" --config-dir "$config_directory" --apps-list "$apps_list" --selector "$selector" >/dev/null
[[ ! -e "$prefix" ]] || fail 'Dry run created installation files'
assert_equals "$(/usr/bin/wc -l < "$apps_list")" 1

"$project_directory/scripts/install-desktop.sh" --build-dir "$build_directory" \
    --prefix "$prefix" --config-dir "$config_directory" --apps-list "$apps_list" --selector "$selector" >/dev/null
assert_file "$prefix/bin/scrabble"
assert_file "$prefix/bin/applications/io.github.scrabble.desktop"
assert_file "$prefix/share/icons/hicolor/scalable/apps/io.github.scrabble.svg"
[[ -L "$prefix/share/applications/io.github.scrabble.desktop" ]] || fail 'Desktop registration is not a symlink'
assert_contains "$prefix/bin/applications/io.github.scrabble.desktop" 'Name=Scrabble'
assert_contains "$prefix/bin/applications/io.github.scrabble.desktop" "Exec=\"$prefix/bin/scrabble\" --config-dir \"$config_directory\""
assert_contains "$apps_list" "io.github.scrabble.desktop|$prefix/share/icons/hicolor/scalable/apps/io.github.scrabble.svg|Scrabble"
assert_contains "$apps_list" 'other.desktop|/icons/other.svg|Other'
assert_contains "$log" "theme follow desktop $selector --dry-run"
assert_contains "$log" "theme follow desktop $selector"
assert_file "$config_directory/theme.json"

printf '%s\n' 'previous executable' > "$prefix/bin/scrabble"
printf '%s\n' 'previous desktop' > "$prefix/bin/applications/io.github.scrabble.desktop"
printf '%s\n' 'previous icon' > "$prefix/share/icons/hicolor/scalable/apps/io.github.scrabble.svg"
"$project_directory/scripts/install-desktop.sh" --build-dir "$build_directory" \
    --prefix "$prefix" --config-dir "$config_directory" --apps-list "$apps_list" --selector "$selector" >/dev/null
assert_backup_contains "$prefix/bin/scrabble.scrabble-backup.*" 'previous executable'
assert_backup_contains "$prefix/bin/applications/io.github.scrabble.desktop.scrabble-backup.*" 'previous desktop'
assert_backup_contains "$prefix/share/icons/hicolor/scalable/apps/io.github.scrabble.svg.scrabble-backup.*" 'previous icon'
assert_equals "$(/usr/bin/awk -F '|' '$1 == "io.github.scrabble.desktop" { count++ } END { print count + 0 }' "$apps_list")" 1

"$project_directory/scripts/install-desktop.sh" --build-dir "$build_directory" \
    --prefix "$test_root/portable" --config-dir "$config_directory" --apps-list "$test_root/portable/apps.list" \
    --selector "$selector" --no-follow >/dev/null
assert_equals "$(/usr/bin/wc -l < "$log")" 5

"$project_directory/scripts/install-desktop.sh" --build-dir "$build_directory" \
    --prefix "$test_root/no-list" --config-dir "$config_directory" --no-apps-list --no-follow >/dev/null
[[ ! -e "$test_root/no-list/apps.list" ]] || fail '--no-apps-list created an app list'

unsafe_prefix="$test_root/unsafe"
/usr/bin/mkdir -p -- "$unsafe_prefix/bin"
printf '%s\n' 'protected executable' > "$test_root/protected"
/usr/bin/ln -s -- "$test_root/protected" "$unsafe_prefix/bin/scrabble"
if "$project_directory/scripts/install-desktop.sh" --build-dir "$build_directory" \
    --prefix "$unsafe_prefix" --config-dir "$test_root/unsafe-config" --no-apps-list --no-follow >/dev/null 2>&1; then
    fail 'Installer accepted a symlinked executable'
fi
assert_contains "$test_root/protected" 'protected executable'
/usr/bin/ln -s -- "$test_root/protected" "$test_root/apps-list-link"

if "$project_directory/scripts/install-desktop.sh" --build-dir "$build_directory" \
    --prefix "$test_root/list-link-prefix" --config-dir "$test_root/list-link-config" \
    --apps-list "$test_root/apps-list-link" --no-follow >/dev/null 2>&1; then
    fail 'Installer accepted a symlinked app list'
fi

/usr/bin/mkdir -p -- "$test_root/nested-link-prefix/share" "$test_root/outside"
/usr/bin/ln -s -- "$test_root/outside" "$test_root/nested-link-prefix/share/icons"
"$project_directory/scripts/install-desktop.sh" --build-dir "$build_directory" \
    --prefix "$test_root/nested-link-prefix" --config-dir "$test_root/nested-link-config" \
    --no-apps-list --no-follow >/dev/null
assert_file "$test_root/outside/hicolor/scalable/apps/io.github.scrabble.svg"

printf '%s\n' 'install tests passed'
