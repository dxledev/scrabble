#include "cli.h"
#include "core/storage.h"
#include "core/theme.h"
#include "core/theme_source.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QTextStream>
#include <limits>
#include <stdexcept>

namespace scrabble {
namespace {

[[noreturn]] void fail(const QString &message) {
    throw std::invalid_argument(message.toStdString());
}

void requireCount(const QStringList &arguments, int count) {
    if (arguments.size() != count)
        fail("Invalid arguments. Run scrabble --help for usage.");
}

Theme currentTheme(const QString &path) {
    return QFileInfo::exists(path) ? loadTheme(path) : preset("slate");
}

void printTheme(const Theme &theme) {
    QTextStream(stdout) << QJsonDocument(theme.toJson()).toJson(QJsonDocument::Indented);
}

Theme proposedTheme(const Theme &current, const QString &command, const QStringList &arguments, const QString &outputPath) {
    if (command == "preset") {
        requireCount(arguments, 1);
        return preset(arguments.first());
    }
    if (command == "set")
        return setColors(current, arguments);
    if (command == "import") {
        requireCount(arguments, 1);
        auto imported = importPalette(QDir::cleanPath(QFileInfo(arguments.first()).absoluteFilePath()));
        imported.sourceKind.clear();
        imported.sourcePath.clear();
        return imported;
    }
    if (command == "detach") {
        requireCount(arguments, 0);
        auto detached = current;
        detached.sourceKind.clear();
        detached.sourcePath.clear();
        return detached;
    }
    if (command == "follow") {
        if (arguments.isEmpty() || arguments.size() > 2)
            fail("Expected theme follow SOURCE [PATH]");
        const auto kind = arguments.first() == "auto" ? QString("desktop") : arguments.first();
        auto result = bindSource(current, kind, arguments.value(1));
        if (QFileInfo(result.sourcePath).canonicalFilePath() == QFileInfo(outputPath).canonicalFilePath()
            || QDir::cleanPath(result.sourcePath) == QDir::cleanPath(outputPath))
            fail("A theme cannot follow its own output file");
        return result;
    }
    fail("Unknown theme command: " + command);
}

}

Options parseOptions(const QStringList &arguments) {
    Options result;
    result.configDirectory = defaultConfigDirectory();
    for (int index = 0; index < arguments.size(); ++index) {
        const auto argument = arguments[index];
        if (argument == "--config-dir" || argument == "--level" || argument == "--seed") {
            if (++index >= arguments.size() || arguments[index].startsWith("--"))
                fail(argument + " requires a value");
            const auto value = arguments[index];
            if (argument == "--config-dir") {
                if (value.trimmed().isEmpty())
                    fail("--config-dir requires a directory");
                result.configDirectory = QDir(value).absolutePath();
            } else if (argument == "--level") {
                if (!QStringList{"easy", "medium", "hard"}.contains(value))
                    fail("--level requires easy, medium, or hard");
                result.level = value;
            } else {
                bool valid = false;
                const auto seed = value.toULongLong(&valid);
                if (!valid || seed > std::numeric_limits<std::uint32_t>::max() || value.startsWith('-'))
                    fail("--seed requires an unsigned 32-bit integer");
                result.seed = static_cast<std::uint32_t>(seed);
            }
        } else if (argument == "--dry-run") {
            result.dryRun = true;
        } else if (argument == "--help" || argument == "-h") {
            result.help = true;
        } else if (argument == "--version") {
            result.version = true;
        } else if (argument.startsWith('-')) {
            fail("Unknown option: " + argument);
        } else {
            result.commands.append(argument);
        }
    }
    return result;
}

void printHelp() {
    QTextStream(stdout) << R"(Scrabble — a portable, standalone word game against the computer

Usage:
  scrabble [--level easy|medium|hard] [--seed NUMBER] [--config-dir DIRECTORY]
  scrabble theme list|keys|path|show
  scrabble theme preset forest|paper|rose|slate [--dry-run]
  scrabble theme set 'accent=#b2c7ff' 'opacity=0.9' [--dry-run]
  scrabble theme import FILE [--dry-run]
  scrabble theme export FILE [--dry-run]
  scrabble theme follow file|static PATH [--dry-run]
  scrabble theme detach [--dry-run]

Colors accept #RRGGBB. Importing a palette also reads #AARRGGBB (alpha first) and
Qt.rgba(r, g, b, a), keeping the background opacity. Theme changes atomically replace
theme.json and every open window reloads its colors immediately, with no restart.
Import, preset, set, and detach stop following a source.
`follow file PATH` tracks a palette file (.qml, .json, or terminal .conf); `follow static
PATH` tracks a theme folder. Followed sources are watched, including in-place edits, atomic
replacement, and symlinks that change target.
--dry-run prints the proposed JSON and leaves files unchanged.
All commands accept --config-dir or the SCRABBLE_CONFIG_DIR environment variable.

Playing:
  Click a rack tile, then a square (or drag).   Arrow keys move the cursor.
  A-Z place a letter from the rack at the cursor; Space flips the direction.
  Enter           Play the word          Backspace   Recall the last tile
  Esc / Delete    Recall all tiles       Tab         Shuffle the rack
  Ctrl+E          Exchange tiles         Ctrl+P      Pass
  Ctrl+H          Hint (limited)         Ctrl+N      New game
  Ctrl+I          Statistics             F1          Rules
)";
}

int runThemeCommand(const Options &options) {
    if (options.level || options.seed)
        fail("--level and --seed apply to the game");
    auto arguments = options.commands;
    if (arguments.isEmpty() || arguments.takeFirst() != "theme" || arguments.isEmpty())
        fail("Expected a theme subcommand. Run scrabble --help.");
    const auto command = arguments.takeFirst();
    const auto path = options.configDirectory + "/theme.json";
    if (QStringList{"list", "keys", "path", "show"}.contains(command)) {
        requireCount(arguments, 0);
        if (command == "show")
            printTheme(currentTheme(path));
        else
            QTextStream(stdout) << (command == "list" ? presets().join('\n')
                : command == "keys" ? colorKeys().join('\n') : path) << '\n';
        return 0;
    }
    if (command == "export") {
        requireCount(arguments, 1);
        auto theme = currentTheme(path);
        theme.sourceKind.clear();
        theme.sourcePath.clear();
        if (options.dryRun)
            printTheme(theme);
        else
            saveTheme(QFileInfo(arguments.first()).absoluteFilePath(), theme);
        return 0;
    }
    if (options.dryRun) {
        printTheme(proposedTheme(currentTheme(path), command, arguments, path));
        return 0;
    }
    if (!QDir().mkpath(options.configDirectory))
        fail("Cannot create " + options.configDirectory);
    const auto lockPath = themeLockPath(path);
    if (!QDir().mkpath(QFileInfo(lockPath).absolutePath()))
        fail("Cannot create theme lock directory");
    QLockFile lock(lockPath);
    if (!lock.tryLock(2000))
        fail("Theme is being edited by another process");
    const auto theme = proposedTheme(currentTheme(path), command, arguments, path);
    saveTheme(path, theme);
    QTextStream(stdout) << "Saved " << path << '\n';
    return 0;
}

}
