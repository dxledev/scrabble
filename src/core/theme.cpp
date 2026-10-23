/**
 * @file theme.cpp
 * @brief Theme presets, color token parsing, validation, and the JSON file format.
 *
 * Every color in a theme file is an opaque #RRGGBB value. Window opacity is stored as one
 * integer (0-255) so a followed source that expresses opacity as a float never produces a
 * value that differs from what was saved, which would make a watcher rewrite the file forever.
 */
#include "theme.h"
#include "storage.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace scrabble {
namespace {

// Values follow colorKeys(): background surface surface_alt text muted accent accent_text border
// grid tile tile_text double_letter triple_letter double_word triple_word danger, then the opacity.
const QMap<QString, QStringList> palettes{
    {"forest", {"#101819", "#182324", "#1e2d2e", "#edf3ed", "#91a6a2", "#b6e3c6", "#152c24", "#304243",
                "#536967", "#eff4ee", "#1b2a24", "#78c2b8", "#5fa8d3", "#e8b86a", "#e07a70", "#e07a70"}},
    {"paper", {"#f4f1e9", "#fffcf5", "#ebe7dd", "#292f2c", "#6f7970", "#315c48", "#ffffff", "#ddd8cb",
               "#9aab9b", "#fefdf8", "#232a26", "#7bb7c2", "#4b86bd", "#e0a76a", "#c9524a", "#b9423b"}},
    {"slate", {"#131720", "#1c2230", "#252e40", "#ebeffa", "#97a3be", "#b2c7ff", "#1d2a4b", "#34405a",
               "#576986", "#eef2fb", "#1a2236", "#6cc4d6", "#6f9bec", "#e8c56a", "#ee7788", "#e77882"}},
    {"rose", {"#211a22", "#2c232e", "#382d3b", "#f7eaf1", "#b8a0b2", "#efb7ce", "#3a2131", "#4e3a4c",
              "#85687f", "#fbf1f5", "#2d1d28", "#91d7e3", "#8aadf4", "#f5a97f", "#ed8796", "#ee99a0"}},
};

[[noreturn]] void fail(const QString &message) {
    throw std::invalid_argument(message.toStdString());
}

double channelValue(const QString &expression) {
    const auto parts = expression.trimmed().split('/');
    if (parts.size() > 2)
        fail("Unsupported Qt.rgba channel: " + expression);
    bool valid = false;
    double value = parts[0].trimmed().toDouble(&valid);
    if (!valid)
        fail("Invalid Qt.rgba channel: " + expression);
    if (parts.size() == 2) {
        bool denominatorValid = false;
        const double denominator = parts[1].trimmed().toDouble(&denominatorValid);
        if (!denominatorValid || denominator <= 0)
            fail("Invalid Qt.rgba divisor: " + expression);
        value /= denominator;
    }
    if (!std::isfinite(value) || value < -1e-9 || value > 1 + 1e-9)
        fail("Qt.rgba channels must be between 0 and 1");
    return std::clamp(value, 0.0, 1.0);
}

int toByte(double fraction) {
    return std::clamp(int(std::lround(fraction * 255.0)), 0, 255);
}

}

QColor parseColorToken(const QString &text) {
    const QString value = text.trimmed();
    static const QRegularExpression rgba(R"(^Qt\.rgba\s*\(([^)]*)\)$)");
    if (const auto match = rgba.match(value); match.hasMatch()) {
        const auto channels = match.captured(1).split(',');
        if (channels.size() != 4)
            fail("Qt.rgba needs four channels");
        return QColor::fromRgb(toByte(channelValue(channels[0])), toByte(channelValue(channels[1])),
                               toByte(channelValue(channels[2])), toByte(channelValue(channels[3])));
    }
    static const QRegularExpression hex(R"(^#?([0-9a-fA-F]{2})?([0-9a-fA-F]{6})$)");
    const auto match = hex.match(value);
    if (!match.hasMatch())
        fail("Unsupported color: " + text);
    // Qt writes eight digits as #AARRGGBB: alpha comes first and never shifts the RGB channels.
    const int alpha = match.captured(1).isEmpty() ? 255 : match.captured(1).toInt(nullptr, 16);
    const int rgb = match.captured(2).toInt(nullptr, 16);
    return QColor::fromRgb((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff, alpha);
}

QColor Theme::color(const QString &key) const {
    return colors.value(key);
}

QString Theme::hex(const QString &key) const {
    return color(key).name(QColor::HexRgb);
}

QColor Theme::backgroundWithAlpha() const {
    auto result = color("background");
    result.setAlpha(backgroundAlpha);
    return result;
}

QJsonObject Theme::toJson() const {
    QJsonObject palette;
    for (const auto &key : colorKeys()) {
        const auto value = color(key);
        if (!value.isValid())
            fail("Missing or invalid theme color: " + key);
        palette.insert(key, value.name(QColor::HexRgb));
    }
    QJsonObject result{{"version", 1}, {"name", name}, {"colors", palette}, {"background_alpha", backgroundAlpha}};
    if (!sourceKind.isEmpty())
        result.insert("source", QJsonObject{{"kind", sourceKind}, {"path", sourcePath}});
    return result;
}

bool Theme::operator==(const Theme &other) const {
    return name == other.name && colors == other.colors && backgroundAlpha == other.backgroundAlpha
        && sourceKind == other.sourceKind && sourcePath == other.sourcePath;
}

QStringList colorKeys() {
    return {"background", "surface", "surface_alt", "text", "muted", "accent", "accent_text", "border",
            "grid", "tile", "tile_text", "double_letter", "triple_letter", "double_word", "triple_word", "danger"};
}

QStringList presets() {
    return palettes.keys();
}

Theme preset(const QString &name) {
    if (!palettes.contains(name))
        fail("Unknown preset: " + name + ". Available: " + presets().join(", "));
    Theme theme;
    theme.name = name;
    const auto keys = colorKeys();
    const auto values = palettes.value(name);
    for (int index = 0; index < keys.size(); ++index)
        theme.colors.insert(keys[index], QColor(values[index]));
    return theme;
}

Theme parseTheme(const QJsonObject &data) {
    if (data.value("version").toInt(-1) != 1)
        fail("Theme version must be 1");
    const QString name = data.value("name").toString().trimmed();
    if (name.isEmpty() || name.size() > 60)
        fail("Theme name must contain 1–60 characters");
    if (!data.value("colors").isObject())
        fail("Theme must contain a colors object");

    const auto palette = data.value("colors").toObject();
    const auto keys = colorKeys();
    for (auto entry = palette.constBegin(); entry != palette.constEnd(); ++entry) {
        if (!keys.contains(entry.key()))
            fail("Unknown color: " + entry.key());
    }

    static const QRegularExpression colorPattern("^#[0-9a-fA-F]{6}$");
    Theme theme;
    theme.name = name;
    for (const auto &key : keys) {
        const auto value = palette.value(key).toString();
        if (!colorPattern.match(value).hasMatch())
            fail(key + ": expected a color in #RRGGBB format");
        theme.colors.insert(key, QColor(value));
    }

    if (data.contains("background_alpha")) {
        const auto alpha = data.value("background_alpha");
        if (!alpha.isDouble() || alpha.toDouble() != alpha.toInt(-1) || alpha.toInt(-1) < 0 || alpha.toInt(-1) > 255)
            fail("background_alpha must be an integer from 0 to 255");
        theme.backgroundAlpha = alpha.toInt();
    }

    if (data.contains("source")) {
        if (!data.value("source").isObject())
            fail("Theme source must be an object");
        const auto source = data.value("source").toObject();
        theme.sourceKind = source.value("kind").toString();
        theme.sourcePath = source.value("path").toString();
        if (!QStringList{"desktop", "static", "file", "caelestia", "noctalia"}.contains(theme.sourceKind)
            || theme.sourcePath.isEmpty() || !QFileInfo(theme.sourcePath).isAbsolute())
            fail("Theme source needs a known kind and an absolute path");
    }
    return theme;
}

Theme normalized(const Theme &theme) {
    return parseTheme(theme.toJson());
}

Theme loadTheme(const QString &path) {
    return parseTheme(readJson(path));
}

void saveTheme(const QString &path, const Theme &theme) {
    const auto validated = normalized(theme);
    writeJson(path, validated.toJson());
}

QString themeLockPath(const QString &outputPath) {
    const auto configDirectory = QFileInfo(outputPath).absolutePath();
    return QDir::cleanPath(QDir(configDirectory).filePath(".locks/theme.lock"));
}

Theme ensureTheme(const QString &path) {
    if (QFileInfo::exists(path))
        return loadTheme(path);
    const auto theme = preset("slate");
    saveTheme(path, theme);
    return theme;
}

Theme setColors(Theme theme, const QStringList &assignments) {
    if (assignments.isEmpty())
        fail("Provide at least one COLOR=#RRGGBB or opacity=VALUE assignment");
    auto data = theme.toJson();
    auto palette = data.value("colors").toObject();
    for (const auto &assignment : assignments) {
        const auto separator = assignment.indexOf('=');
        if (separator < 1)
            fail("Expected COLOR=#RRGGBB. Colors: " + colorKeys().join(", ") + ", opacity");
        const auto key = assignment.left(separator);
        auto value = assignment.mid(separator + 1).trimmed();
        if (key == "opacity") {
            const bool percent = value.endsWith('%');
            if (percent)
                value.chop(1);
            bool valid = false;
            const double amount = value.toDouble(&valid) / (percent ? 100.0 : 1.0);
            if (!valid || amount < 0 || amount > 1)
                fail("opacity expects a number from 0 to 1, or a percentage");
            data.insert("background_alpha", toByte(amount));
            continue;
        }
        if (!colorKeys().contains(key))
            fail("Unknown color: " + key + ". Colors: " + colorKeys().join(", ") + ", opacity");
        palette.insert(key, value);
    }
    data.insert("name", "custom");
    data.remove("source");
    data.insert("colors", palette);
    return parseTheme(data);
}

}
