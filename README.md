# Scrabble

A portable, standalone Scrabble client written in C++20 and Qt 6. Play a full game against the computer at three strengths, in a resizable native window with a flat, modern look (solid colors, no gradients). Every color is themeable from the command line, and open windows recolor live without losing your game.

- Standard 15 × 15 board, 100 tiles, premium squares, 50-point bingo bonus
- ENABLE word list embedded in the binary as a compact word graph (about 480 KB, loaded once)
- Easy, Medium, and Hard computer opponents
- Hints, tile exchange, pass, undo-by-recall, and a saved game that resumes automatically
- Every popup is a modal inside the window over a blurred snapshot of the game; no separate popup windows
- Vector icons drawn in theme colors; tiles are near-white and tinted by the theme
- Local statistics per level

## Build and run

Requires CMake 3.21+, a C++20 compiler, and Qt 6.7+ (Core, Gui, Widgets; Qt Test for the tests). On Wayland the Qt Wayland plugin is needed. On Arch these come from `cmake`, `ninja`, `gcc`, `qt6-base`, and `qt6-wayland`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
./build/scrabble
```

Install the executable, desktop entry, and scalable icon for your user:

```bash
cmake --install build --prefix "$HOME/.local"
```

`scripts/install-desktop.sh` does the same and also registers the app in an application list file and starts following a theme folder, see [Installing as an application](#installing-as-an-application).

Wayland is selected automatically when `WAYLAND_DISPLAY` is set (override with `QT_QPA_PLATFORM`). Painting is raster/shared-memory, so no GPU context is created. The window can be tiled, floated, maximized, and resized freely; its minimum size is 620 × 760.

## Playing

Place tiles in a single row or column so the word connects to the board. The first word must cover the center star. Every word you form, including cross words, must be in the dictionary; invalid plays are rejected with a message and your tiles stay put.

| Input | Action |
| --- | --- |
| Click a rack tile, then a square | Place the tile; clicking a placed tile returns it |
| A–Z | Place that letter from your rack at the cursor (uses a blank if you have no such tile) |
| Arrow keys | Move the cursor |
| Space | Flip typing direction (a second click on the cursor square does too) |
| Enter | Play the word |
| Backspace | Recall the last tile |
| Esc / Delete | Recall all tiles |
| Tab | Shuffle the rack |
| Ctrl+H | Hint: place the best play as a preview (press Play to use it) |
| Ctrl+E | Exchange tiles (needs 7 or more tiles in the bag) |
| Ctrl+P | Pass |
| Ctrl+N | New game, with a level choice |
| Ctrl+I | Statistics |
| F1 | Rules |

Placing a blank opens an in-window letter picker; you can also just type the letter you want. Scoring follows the standard rules: letter and word premiums apply to newly placed tiles only, blanks score zero, each word formed is scored, and using all seven tiles adds 50. The game ends when a rack and the bag are both empty (the player who went out collects the opponent's remaining tile values; each player loses their own), or after six consecutive scoreless turns.

Opponent strength: **Easy** prefers short words from the lower half of its options, **Medium** plays randomly among options scoring at least 60% of the best, and **Hard** plays the highest-scoring option. Start at a level with `scrabble --level hard`; `--seed N` starts a reproducible game.

The game saves after every turn to `game.json` and resumes on the next launch. Statistics are stored locally in `stats.json`.

## Themes

Themes are one JSON file, `theme.json`, in the configuration directory (`$XDG_CONFIG_HOME/scrabble`, normally `~/.config/scrabble`; override with `--config-dir DIR` or `SCRABBLE_CONFIG_DIR`). Every `theme` command validates the whole palette, writes the file atomically, and any open window reloads its colors immediately. Board, rack, and game state are untouched. Editing the file by hand also reloads live, including editors that replace the file; an invalid edit keeps the last good colors and shows an error.

```bash
scrabble theme list                            # built-in presets: forest, paper, rose, slate
scrabble theme preset paper
scrabble theme set 'accent=#c4b5fd' 'tile=#f5f3ff' 'opacity=0.9'
scrabble theme show
scrabble theme keys
scrabble theme path
scrabble theme export my-theme.json
scrabble theme import my-theme.json
scrabble theme preset rose --dry-run           # print the result, change nothing
```

Colors are `#RRGGBB`. `opacity` (0 to 1, or a percentage) sets the window background's translucency; all other surfaces stay solid for legibility, and the compositor decides whether to blur what is behind a translucent window. The sixteen color keys are `background`, `surface`, `surface_alt`, `text`, `muted`, `accent`, `accent_text`, `border`, `grid`, `tile`, `tile_text`, `double_letter`, `triple_letter`, `double_word`, `triple_word`, and `danger`. Tiles are drawn from `tile` and `tile_text`; the built-in presets and imported palettes keep `tile` near white with a faint tint of the accent.

### Following a palette

A theme can follow an external palette so it recolors whenever that palette changes:

```bash
scrabble theme follow file ~/palettes/colors.qml
scrabble theme follow static ~/.config/themes/ocean
scrabble theme detach
```

Supported palette formats are:

- QML color files (`property color name: "#rrggbb"` or `Qt.rgba(r, g, b, a)`, with channel fractions such as `14 / 255`)
- JSON palettes (`colors`, `colours`, or `palette` objects, and light/dark pairs)
- Terminal `.conf` color files

Eight-digit colors follow Qt's `#AARRGGBB` order (alpha first): `#e00e1e36` is the color `#0e1e36` at 88% opacity. A background written this way, or as `Qt.rgba(14 / 255, 30 / 255, 54 / 255, 0.88)`, sets the window opacity; otherwise only the alpha channel is dropped and the RGB channels are never shifted. Common palette key names (`background`, `foreground`, `primary`, `secondary`, `warning`, `danger`, `surface…`, and several others) are mapped onto the sixteen keys, and missing keys are derived from the ones present.

A followed source is watched, including edits in place, atomic replacement, and symlinks that change target at any level of a chain. The resolved colors are saved to `theme.json`, so the last state survives restarts, and the binding is refreshed on every launch. `theme preset`, `set`, `import`, and `detach` stop following.

## Installing as an application

```bash
scripts/install-desktop.sh --dry-run          # preview
scripts/install-desktop.sh                    # install
scripts/install-desktop.sh --follow ~/.config/themes/current   # also follow a theme folder
```

The script installs `~/.local/bin/scrabble`, the icon, and a `Scrabble` desktop entry under `~/.local/bin/applications` (linked into `~/.local/share/applications`), and adds a `Scrabble` line to `~/.config/apps.list`. The launcher icon is `icon.svg` in the configuration directory; it is redrawn in the theme colors whenever the theme changes. Options: `--prefix`, `--config-dir`, `--apps-list FILE` or `--no-apps-list`, and `--follow PATH` or `--no-follow`. Existing files are backed up before they are replaced.

## Memory and design

The board and rack are each one custom-painted widget. The word list is a minimized word graph in a packed array of 32-bit edges (about 120,000 edges), built at compile time from `data/enable1.txt` and loaded once into a single array. The computer finds plays by walking that graph, so no hash set of words is ever created. The whole UI uses the raster backend and one window.

## Tests

```bash
ctest --test-dir build --output-on-failure
```

Tests cover the word graph, rules and scoring, move generation, serialization, color parsing (including alpha-first and `Qt.rgba` forms), live theme following, widget rendering and clicks, the command line, and the installer.

## Word list

`data/enable1.txt` is the public-domain ENABLE word list. Words longer than 15 letters (which cannot fit the board) are dropped when the graph is built.
