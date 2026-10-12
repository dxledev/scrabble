#include "theme_source.h"
#include "storage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace scrabble {
namespace {

using Palette = QMap<QString, QColor>;

[[noreturn]] void fail(const QString &message) {
    throw std::invalid_argument(message.toStdString());
}

QString configHome() {
    const auto path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    return path.isEmpty() ? QDir::homePath() + "/.config" : path;
}

QString themesHome() {
    return configHome() + "/themes";
}

QString absolutePath(QString path) {
    if (path.startsWith("~/"))
        path = QDir::homePath() + path.mid(1);
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

Palette qmlPalette(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
        fail("Cannot read palette: " + path);
    QString contents = QString::fromUtf8(file.readAll());
    contents.remove(QRegularExpression("/\\*.*?\\*/", QRegularExpression::DotMatchesEverythingOption));
    contents.remove(QRegularExpression("//[^\\n]*"));
    static const QRegularExpression propertyPattern(
        R"(property\s+color\s+(\w+)\s*:\s*(?:["'](#[\da-fA-F]{6}(?:[\da-fA-F]{2})?)["']|Qt\.rgba\(([^)]*)\)))");

    Palette palette;
    auto matches = propertyPattern.globalMatch(contents);
    while (matches.hasNext()) {
        const auto match = matches.next();
        // Alpha is kept here; only the background's alpha survives into the theme.
        palette.insert(match.captured(1), parseColorToken(match.captured(2).isEmpty()
            ? "Qt.rgba(" + match.captured(3) + ")" : match.captured(2)));
    }
    if (palette.isEmpty())
        fail("No supported color properties in " + path);
    return palette;
}

QJsonObject paletteObject(QJsonObject data) {
    for (const auto &key : {"colors", "colours", "palette"}) {
        if (data.value(key).isObject())
            return data.value(key).toObject();
    }
    if (data.value("dark").isObject() || data.value("light").isObject()) {
        const QString mode = data.value("mode").toString("dark");
        data = data.value(mode).toObject();
    }
    for (const auto &key : {"colors", "colours", "palette"}) {
        if (data.value(key).isObject())
            return data.value(key).toObject();
    }
    return data;
}

Palette jsonPalette(const QJsonObject &data) {
    const auto values = paletteObject(data);
    Palette palette;
    for (auto value = values.constBegin(); value != values.constEnd(); ++value) {
        if (!value.value().isString())
            continue;
        const QString text = value.value().toString();
        static const QRegularExpression colorPattern("^#?[0-9a-fA-F]{6}(?:[0-9a-fA-F]{2})?$");
        if (colorPattern.match(text).hasMatch())
            palette.insert(value.key(), parseColorToken(text));
    }
    if (palette.isEmpty())
        fail("No supported colors in palette JSON");
    return palette;
}

Palette terminalPalette(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
        fail("Cannot read terminal palette: " + path);
    static const QRegularExpression linePattern(
        R"(^\s*(background|foreground|cursor|color(?:[0-9]|1[0-5]))\s+(#[0-9a-fA-F]{6})\s*(?:#.*)?$)",
        QRegularExpression::MultilineOption);
    Palette result;
    auto matches = linePattern.globalMatch(QString::fromUtf8(file.readAll()));
    while (matches.hasNext()) {
        const auto match = matches.next();
        result.insert(match.captured(1), QColor(match.captured(2)));
    }
    const QList<QPair<QString, QString>> aliases{{"red", "color1"}, {"green", "color2"},
        {"yellow", "color3"}, {"blue", "color4"}, {"magenta", "color5"}, {"cyan", "color6"}};
    for (const auto &entry : aliases)
        if (result.contains(entry.second))
            result.insert(entry.first, result.value(entry.second));
    if (result.contains("cursor") || result.contains("color4"))
        result.insert("primary", result.value(result.contains("cursor") ? "cursor" : "color4"));
    if (result.isEmpty())
        fail("No supported terminal colors in " + path);
    return result;
}

QString normalizedKey(QString key) {
    key = key.toLower();
    key.remove(QRegularExpression("[^a-z0-9]"));
    return key;
}

QColor pick(const Palette &palette, const QStringList &keys, const QColor &fallback = {}) {
    for (const auto &alias : keys) {
        const auto expected = normalizedKey(alias);
        for (auto entry = palette.constBegin(); entry != palette.constEnd(); ++entry) {
            if (normalizedKey(entry.key()) == expected)
                return entry.value();
        }
    }
    if (!fallback.isValid())
        fail("Palette is missing " + keys.join(" / "));
    return fallback;
}

QColor mix(const QColor &base, const QColor &tint, double amount) {
    return QColor::fromRgbF(base.redF() * (1 - amount) + tint.redF() * amount,
                            base.greenF() * (1 - amount) + tint.greenF() * amount,
                            base.blueF() * (1 - amount) + tint.blueF() * amount);
}

double luminance(const QColor &color) {
    const auto linear = [](double component) {
        return component <= .04045 ? component / 12.92 : std::pow((component + .055) / 1.055, 2.4);
    };
    return .2126 * linear(color.redF()) + .7152 * linear(color.greenF()) + .0722 * linear(color.blueF());
}

QColor opaque(QColor color) {
    color.setAlpha(255);
    return color;
}

Theme mapPalette(const Palette &palette, const QString &name) {
    const QColor backgroundSource = pick(palette, {"background", "base", "m3background", "mBackground", "mSurface", "surface"});
    const QColor background = opaque(backgroundSource);
    const QColor text = opaque(pick(palette, {"foreground", "onBackground", "on_background", "text", "mOnSurface", "onSurface", "on_surface", "m3onBackground", "m3onSurface"}));
    const QColor accent = opaque(pick(palette, {"primary", "accent", "mPrimary", "m3primary"}));
    const QColor surface = opaque(pick(palette, {"surface_container_low", "surfaceContainerLow", "backgroundAlt", "mantle", "surface0", "mSurfaceVariant"}, mix(background, text, .035)));
    const QColor alternate = opaque(pick(palette, {"surface_container", "surfaceContainer", "backgroundGray", "surface1", "mSurfaceVariant"}, mix(background, text, .07)));
    const QColor danger = opaque(pick(palette, {"danger", "error", "mError", "red"}, QColor("#e06c75")));
    const QColor border = opaque(pick(palette, {"border", "outline", "mOutline"}, mix(surface, accent, .25)));
    const QColor muted = opaque(pick(palette, {"foregroundInactive", "on_surface_variant", "onSurfaceVariant", "mOnSurfaceVariant", "muted"}, mix(background, text, .65)));
    const QColor onAccent = luminance(accent) > .4 ? QColor("#151515") : QColor("#ffffff");

    Theme theme;
    theme.name = name;
    theme.backgroundAlpha = backgroundSource.alpha();
    theme.colors = {
        {"background", background}, {"surface", surface}, {"surface_alt", alternate},
        {"text", text}, {"muted", muted}, {"accent", accent},
        {"accent_text", opaque(pick(palette, {"on_primary", "onPrimary", "mOnPrimary", "m3onPrimary"}, onAccent))},
        {"border", mix(surface, border, .35)}, {"grid", mix(surface, border, .6)},
        // Tiles stay near-white; the accent only tints them so they follow the theme.
        {"tile", mix(QColor("#ffffff"), accent, .08)}, {"tile_text", mix(QColor("#14161c"), accent, .12)},
        {"double_letter", opaque(pick(palette, {"double_letter", "secondary", "mSecondary", "m3secondary", "cyan", "teal"}, mix(accent, text, .25)))},
        {"triple_letter", accent},
        {"double_word", opaque(pick(palette, {"double_word", "warning", "tertiary", "mTertiary", "m3tertiary", "yellow", "orange"}, mix(accent, text, .45)))},
        {"triple_word", danger}, {"danger", danger},
    };
    // Match the 8-bit channels of the saved file so a reload never looks like a change.
    return normalized(theme);
}

QString expandedBinding(const QString &kind, const QString &requested) {
    if (kind == "desktop")
        return absolutePath(requested.isEmpty() ? themesHome() + "/.caelestia-use" : requested);
    if (kind == "static") {
        if (requested.isEmpty())
            return absolutePath(themesHome() + "/current");
        if (requested.startsWith("~/") || QFileInfo(requested).isAbsolute())
            return absolutePath(requested);
        if (requested.startsWith("./") || requested.startsWith("../"))
            return absolutePath(requested);
        return absolutePath(QDir(themesHome()).absoluteFilePath(requested));
    }
    if (kind == "file") {
        if (requested.isEmpty())
            fail("file source needs a palette file path");
        return absolutePath(requested);
    }
    if (kind == "caelestia") {
        const auto path = requested.isEmpty() ? themesHome() + "/.dynamic/caelestia/palette.json" : requested;
        return absolutePath(path);
    }
    if (kind == "noctalia") {
        const auto path = requested.isEmpty() ? themesHome() + "/.dynamic/noctalia" : requested;
        return absolutePath(path);
    }
    fail("Unknown source: " + kind);
}

QString directoryPaletteFile(const QString &directory, const QString &kind) {
    QStringList names;
    if (kind == "caelestia")
        names = {"palette.json", "Colors.qml", "noctalia.json"};
    else if (kind == "noctalia")
        names = {"Colors.qml", "noctalia.json", "palette.json"};
    else
        names = {"theme.json", "Colors.qml", "palette.json", "colors.json", "noctalia.json", "kitty.conf"};
    for (const auto &name : names) {
        const auto path = QDir(directory).filePath(name);
        if (QFileInfo(path).isFile())
            return path;
    }
    return QDir(directory).filePath(names.front());
}

QString resolvedPaletteFile(const QString &kind, const QString &binding) {
    const QFileInfo info(binding);
    if (info.isDir())
        return directoryPaletteFile(info.absoluteFilePath(), kind);
    if (info.isFile() || info.isSymLink())
        return info.absoluteFilePath();
    if (binding.endsWith(".qml", Qt::CaseInsensitive) || binding.endsWith(".json", Qt::CaseInsensitive))
        return binding;
    return directoryPaletteFile(binding, kind);
}

QJsonObject readPaletteJson(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        fail("Cannot read palette: " + path + ": " + file.errorString());
    if (file.size() > 1024 * 1024)
        fail("Palette JSON exceeds 1 MiB: " + path);
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        fail(path + ": expected a valid JSON object (" + error.errorString() + ")");
    return document.object();
}

Palette readPalette(const QString &path) {
    if (path.endsWith(".qml", Qt::CaseInsensitive))
        return qmlPalette(path);
    if (path.endsWith(".conf", Qt::CaseInsensitive))
        return terminalPalette(path);
    return jsonPalette(readPaletteJson(path));
}

Theme importDirectory(const QString &directory, const QString &primary, const QString &name) {
    auto colors = readPalette(primary);
    for (const auto &file : {"palette.json", "kitty.conf"}) {
        const auto companion = QDir(directory).filePath(file);
        if (companion == primary || !QFileInfo(companion).isFile())
            continue;
        const auto extra = readPalette(companion);
        for (auto entry = extra.constBegin(); entry != extra.constEnd(); ++entry)
            if (!colors.contains(entry.key()))
                colors.insert(entry.key(), entry.value());
    }
    return mapPalette(colors, name);
}

QString sourceName(const QString &kind, const QString &binding, const QString &paletteFile) {
    if (kind == "desktop" || kind == "static") {
        auto directory = QFileInfo(paletteFile).canonicalPath();
        if (directory.isEmpty())
            directory = QFileInfo(binding).fileName();
        return kind + ": " + QFileInfo(directory).fileName();
    }
    return kind + ": " + QFileInfo(paletteFile).completeBaseName();
}

void addExistingPathAndParent(QString path, QStringList &result) {
    path = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    QFileInfo info(path);
    if (info.exists() || info.isSymLink()) {
        result.append(path);
        const auto parent = info.absoluteDir().absolutePath();
        if (QFileInfo(parent).isDir())
            result.append(parent);
        return;
    }
    auto parent = info.absoluteDir();
    while (!parent.isRoot() && !QFileInfo(parent.absolutePath()).isDir())
        parent.cdUp();
    if (QFileInfo(parent.absolutePath()).isDir())
        result.append(parent.absolutePath());
}

}

Theme importPalette(const QString &path, const QString &name) {
    const QString absolute = absolutePath(path);
    const QString paletteName = name.isEmpty() ? QFileInfo(absolute).completeBaseName() : name;
    if (absolute.endsWith(".qml", Qt::CaseInsensitive))
        return mapPalette(qmlPalette(absolute), paletteName);
    if (absolute.endsWith(".conf", Qt::CaseInsensitive))
        return mapPalette(terminalPalette(absolute), paletteName);

    const auto data = readPaletteJson(absolute);
    if (data.value("version").toInt() == 1 && data.value("colors").isObject()
        && data.value("colors").toObject().contains("double_word")) {
        auto theme = parseTheme(data);
        if (!name.isEmpty())
            theme.name = name;
        return theme;
    }
    return mapPalette(jsonPalette(data), paletteName);
}

Theme bindSource(const Theme &current, const QString &kind, const QString &path) {
    Q_UNUSED(current);
    if (!QStringList{"desktop", "static", "file", "caelestia", "noctalia"}.contains(kind))
        fail("Unknown source: " + kind);
    const QString binding = expandedBinding(kind, path);
    const QString paletteFile = resolvedPaletteFile(kind, binding);
    const auto name = sourceName(kind, binding, paletteFile);
    auto theme = QFileInfo(binding).isDir() && !paletteFile.endsWith("/theme.json")
        ? importDirectory(binding, paletteFile, name) : importPalette(paletteFile, name);
    theme.sourceKind = kind;
    theme.sourcePath = binding;
    return theme;
}

Theme refreshSource(const Theme &theme) {
    if (theme.sourceKind.isEmpty())
        return theme;
    return bindSource(theme, theme.sourceKind, theme.sourcePath);
}

QStringList sourceWatchPaths(const Theme &theme) {
    if (theme.sourceKind.isEmpty() || theme.sourcePath.isEmpty())
        return {};
    QStringList result;
    addExistingPathAndParent(theme.sourcePath, result);
    const auto canonicalBinding = QFileInfo(theme.sourcePath).canonicalFilePath();
    if (!canonicalBinding.isEmpty())
        addExistingPathAndParent(canonicalBinding, result);
    const QString paletteFile = resolvedPaletteFile(theme.sourceKind, theme.sourcePath);
    addExistingPathAndParent(paletteFile, result);
    const auto canonicalPalette = QFileInfo(paletteFile).canonicalFilePath();
    if (!canonicalPalette.isEmpty())
        addExistingPathAndParent(canonicalPalette, result);

    result.removeDuplicates();
    return result;
}

}
