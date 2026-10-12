#pragma once

#include <QColor>
#include <QJsonObject>
#include <QMap>
#include <QStringList>

namespace scrabble {

/** @brief Validated palette with an optional window opacity and an optional followed source. */
struct Theme {
    QString name;                 ///< Display name or preset identifier.
    QMap<QString, QColor> colors; ///< Opaque colors keyed by colorKeys().
    int backgroundAlpha = 255;    ///< Window background opacity, 0 (clear) to 255 (solid).
    QString sourceKind;           ///< Followed source type, empty when none.
    QString sourcePath;           ///< Absolute path of the followed source.

    /** @brief Read a configured color by key. */
    QColor color(const QString &key) const;
    /** @brief Return a color as #RRGGBB. */
    QString hex(const QString &key) const;
    /** @brief Window background with its opacity applied. */
    QColor backgroundWithAlpha() const;
    /** @brief Serialize to the version-1 theme file format. */
    QJsonObject toJson() const;
    bool operator==(const Theme &other) const;
    bool operator!=(const Theme &other) const { return !(*this == other); }
};

/** @brief Required palette keys in canonical order. */
QStringList colorKeys();
/** @brief Built-in preset names. */
QStringList presets();
/** @brief Build a preset by name; throws on unknown names. */
Theme preset(const QString &name);

/**
 * @brief Parse one color token: #RRGGBB, #AARRGGBB (alpha first, as Qt writes it) or
 * Qt.rgba(r, g, b, a) where each channel may be written as a fraction such as 14 / 255.
 * The result keeps its alpha. Throws std::invalid_argument on malformed input.
 */
QColor parseColorToken(const QString &text);

/** @brief Parse and validate a complete theme object. */
Theme parseTheme(const QJsonObject &data);
/** @brief Load and validate a theme JSON file. */
Theme loadTheme(const QString &path);
/** @brief Validate and atomically write a theme. */
void saveTheme(const QString &path, const Theme &theme);
/** @brief Path of the lock file that serializes theme edits. */
QString themeLockPath(const QString &outputPath);
/** @brief Load the theme file, creating the default one when absent. */
Theme ensureTheme(const QString &path);
/** @brief Apply key=value assignments; accepts COLOR=#RRGGBB and opacity=0..1 or N%. */
Theme setColors(Theme theme, const QStringList &assignments);
/**
 * @brief Write icon.svg into @p directory with the theme's colors (atomic, best effort, never throws).
 *
 * The launcher icon follows the theme the same way the window does: tile faces use "tile",
 * letters "tile_text", the plate "surface_alt", and the highlighted tile "accent".
 */
void writeThemedIcon(const QString &directory, const Theme &theme);
/** @brief Round-trip through the file format so every channel is 8-bit exact. */
Theme normalized(const Theme &theme);

}
