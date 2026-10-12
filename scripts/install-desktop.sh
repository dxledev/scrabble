#!/usr/bin/env bash
set -euo pipefail

project_directory="$(cd -- "${BASH_SOURCE[0]%/*}/.." && pwd)"
build_directory="$project_directory/build"
game_binary="$build_directory/scrabble"
install_prefix="${HOME:?}/.local"
config_directory="${XDG_CONFIG_HOME:-$HOME/.config}/scrabble"
apps_list="${XDG_CONFIG_HOME:-$HOME/.config}/apps.list"
selector="${XDG_CONFIG_HOME:-$HOME/.config}/themes/.caelestia-use"
follow_theme=1
use_apps_list=1
dry_run=0
desktop_id="io.github.scrabble"
icon_directory=""
icon_file=""

usage() {
    printf '%s\n' 'Usage: install-desktop.sh [--dry-run] [--build-dir DIR] [--prefix DIR]' \
        '       [--config-dir DIR] [--apps-list FILE | --no-apps-list]' \
        '       [--selector PATH | --follow PATH | --no-follow]'
}

fail() {
    printf 'install-desktop: %s\n' "$*" >&2
    exit 1
}

require_safe_path() {
    local option="$1" path="$2"
    [[ "$path" == /* && "$path" != *$'\n'* && "$path" != *'|'* && "$path" != *'"'* && "$path" != *'%'* && "$path" != *'\\'* ]] ||
        fail "Expected an absolute path without desktop-entry or app-list delimiters for $option"
}

parse_options() {
    while (( $# )); do
        case "$1" in
            --dry-run) dry_run=1; shift ;;
            --help|-h) usage; exit 0 ;;
            --no-follow) follow_theme=0; shift ;;
            --no-apps-list) use_apps_list=0; shift ;;
            --build-dir|--prefix|--config-dir|--apps-list|--selector|--follow)
                (( $# >= 2 )) || fail "Missing value for $1"
                require_safe_path "$1" "$2"
                case "$1" in
                    --build-dir) build_directory="${2%/}" ;;
                    --prefix) install_prefix="${2%/}" ;;
                    --config-dir) config_directory="${2%/}" ;;
                    --apps-list) apps_list="$2"; use_apps_list=1 ;;
                    --selector|--follow) selector="${2%/}"; follow_theme=1 ;;
                esac
                shift 2 ;;
            *) fail "Unknown option: $1" ;;
        esac
    done
}

resolve_paths() {
    game_binary="$build_directory/scrabble"
    if [[ ! -x "$game_binary" && -x "$project_directory/scrabble" ]]; then
        game_binary="$project_directory/scrabble"
    fi
    icon_directory="$(/usr/bin/readlink -m -- "$install_prefix/share/icons/hicolor/scalable/apps")"
    icon_file="$icon_directory/$desktop_id.svg"
}

validate_targets() {
    local executable="$install_prefix/bin/scrabble"
    local desktop="$install_prefix/bin/applications/$desktop_id.desktop"
    local registered="$install_prefix/share/applications/$desktop_id.desktop"
    [[ -x "$game_binary" && ! -L "$game_binary" ]] || fail "Build the game first: $build_directory/scrabble"
    [[ -n "$install_prefix" && "$install_prefix" != / ]] || fail 'Invalid installation prefix'
    [[ "$config_directory" != / && -n "$config_directory" ]] || fail 'Invalid configuration directory'
    if (( use_apps_list )); then
        [[ ! -e "$apps_list" || -f "$apps_list" ]] || fail "App list is not a regular file: $apps_list"
        [[ ! -L "$apps_list" ]] || fail "Refusing to modify a symlinked app list: $apps_list"
    fi
    [[ ! -L "$executable" ]] || fail "Refusing to replace a symlinked executable: $executable"
    [[ ! -L "$desktop" ]] || fail "Refusing to replace a symlinked desktop entry: $desktop"
    [[ ! -L "$icon_file" ]] || fail "Refusing to replace a symlinked icon: $icon_file"
    [[ ! -L "$config_directory/theme.json" ]] || fail "Refusing to replace a symlinked theme file: $config_directory/theme.json"
    [[ ! -e "$registered" && ! -L "$registered" || -L "$registered" && "$(/usr/bin/readlink -- "$registered")" == "$desktop" ]] ||
        fail "Existing desktop registration is unmanaged: $registered"
    if (( follow_theme )) && { [[ -e "$selector" ]] || [[ -L "$selector" ]]; }; then
        [[ -f "$selector" || -d "$selector" ]] || fail "Theme selector must resolve to a file or directory: $selector"
        "$game_binary" --config-dir "$config_directory" theme follow desktop "$selector" --dry-run >/dev/null
    fi
}

backup_file() {
    local file="$1" backup
    [[ -f "$file" ]] || return 0
    backup="${file}.scrabble-backup.$(/usr/bin/date +%Y%m%dT%H%M%S).$$"
    [[ ! -e "$backup" && ! -L "$backup" ]] || fail "Backup already exists: $backup"
    /usr/bin/cp -p -- "$file" "$backup"
}

replace_executable() {
    local destination="$install_prefix/bin/scrabble" temporary
    /usr/bin/mkdir -p -- "${destination%/*}"
    backup_file "$destination"
    temporary="$(/usr/bin/mktemp "${destination%/*}/.scrabble-binary.XXXXXX")"
    /usr/bin/install -m755 -- "$game_binary" "$temporary"
    /usr/bin/mv -T -- "$temporary" "$destination"
}

replace_desktop() {
    local source="$project_directory/assets/$desktop_id.desktop"
    local desktop="$install_prefix/bin/applications/$desktop_id.desktop"
    local registered="$install_prefix/share/applications/$desktop_id.desktop"
    local executable="$install_prefix/bin/scrabble" temporary
    /usr/bin/mkdir -p -- "${desktop%/*}" "${registered%/*}"
    backup_file "$desktop"
    temporary="$(/usr/bin/mktemp "${desktop%/*}/.scrabble-desktop.XXXXXX")"
    /usr/bin/awk -v executable="$executable" -v config="$config_directory" \
        '/^Exec=/ { print "Exec=\"" executable "\" --config-dir \"" config "\""; next } { print }' \
        "$source" > "$temporary"
    /usr/bin/chmod 644 -- "$temporary"
    /usr/bin/mv -T -- "$temporary" "$desktop"
    [[ -L "$registered" ]] || /usr/bin/ln -s -- "$desktop" "$registered"
}

replace_icon() {
    local destination="$icon_file" temporary
    /usr/bin/mkdir -p -- "${destination%/*}"
    backup_file "$destination"
    temporary="$(/usr/bin/mktemp "${destination%/*}/.scrabble-icon.XXXXXX")"
    /usr/bin/install -m644 -- "$project_directory/assets/$desktop_id.svg" "$temporary"
    /usr/bin/mv -T -- "$temporary" "$destination"
}

# The launcher icon lives beside theme.json; the game recolors it whenever the theme changes.
ensure_launcher_icon() {
    local destination="$config_directory/icon.svg" temporary
    [[ ! -L "$destination" ]] || fail "Refusing to replace a symlinked icon: $destination"
    [[ ! -e "$destination" ]] || return 0
    /usr/bin/mkdir -p -- "${destination%/*}"
    temporary="$(/usr/bin/mktemp "${destination%/*}/.scrabble-icon.XXXXXX")"
    /usr/bin/install -m644 -- "$project_directory/assets/$desktop_id.svg" "$temporary"
    /usr/bin/mv -T -- "$temporary" "$destination"
}

register_app() {
    (( use_apps_list )) || return 0
    local launcher_icon="$config_directory/icon.svg"
    local entry="$desktop_id.desktop|$launcher_icon|Scrabble"
    local temporary
    /usr/bin/mkdir -p -- "${apps_list%/*}"
    temporary="$(/usr/bin/mktemp "${apps_list%/*}/.scrabble-apps.XXXXXX")"
    if [[ -f "$apps_list" ]]; then
        /usr/bin/awk -F '|' -v OFS='|' -v id="$desktop_id.desktop" \
            -v icon="$launcher_icon" \
            '$1 == id { if (!found) { $2 = icon; $3 = "Scrabble"; print; found = 1 } next } { print } END { if (!found) print id, icon, "Scrabble" }' \
            "$apps_list" > "$temporary"
        /usr/bin/chmod --reference="$apps_list" "$temporary"
    else
        printf '%s\n' "$entry" > "$temporary"
    fi
    backup_file "$apps_list"
    /usr/bin/mv -T -- "$temporary" "$apps_list"
}

configure_theme() {
    (( follow_theme )) && [[ -e "$selector" ]] || return 0
    backup_file "$config_directory/theme.json"
    "$install_prefix/bin/scrabble" --config-dir "$config_directory" theme follow desktop "$selector" >/dev/null
}

main() {
    parse_options "$@"
    resolve_paths
    validate_targets
    local desktop="$install_prefix/bin/applications/$desktop_id.desktop"
    local registered="$install_prefix/share/applications/$desktop_id.desktop"
    local icon="$icon_file"
    if (( dry_run )); then
        printf 'Install executable: %s/bin/scrabble\nDesktop entry: %s\nRegister desktop: %s\nIcon: %s\nLauncher icon: %s/icon.svg\nApp list: %s\n' \
            "$install_prefix" "$desktop" "$registered" "$icon" "$config_directory" "$apps_list"
        (( use_apps_list )) || printf 'App list: disabled\n'
        if (( follow_theme )) && [[ -e "$selector" ]]; then
            printf 'Follow theme selector: %s\nSave theme: %s/theme.json\n' "$selector" "$config_directory"
        fi
        return
    fi
    replace_executable
    replace_icon
    replace_desktop
    configure_theme
    ensure_launcher_icon
    register_app
    printf 'Installed Scrabble.\n'
}

main "$@"
