/**
 * @file theme_tests.cpp
 * @brief Color token parsing, palette mapping, alpha handling, and live source following.
 */
#include "core/storage.h"
#include "core/theme.h"
#include "core/theme_source.h"
#include "ui/theme_watcher.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>
#include <cstdio>

using namespace scrabble;

namespace {

void write(const QString &path, const QString &contents) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
    file.write(contents.toUtf8());
}

QString colors(const QString &background, const QString &primary = "\"#9279aa\"") {
    return QString(R"(pragma Singleton
import Quickshell
import QtQuick
Singleton {
    readonly property color background: %1
    readonly property color backgroundAlt: "#0E1E36"
    readonly property color backgroundAlpha: Qt.rgba(26 / 255, 42 / 255, 70 / 255, 0.88)
    readonly property color backgroundGray: "#2a3a56"
    readonly property color foreground: "#f4b999"
    readonly property color foregroundInactive: "#fa7e75"
    readonly property color primary: %2
    readonly property color muted: "#574f72"
    readonly property color border: "#9279aa"
    readonly property color secondary: "#c85670"
    readonly property color warning: "#f4b999"
    readonly property color danger: "#d2495b"
}
)").arg(background, primary);
}

double luminance(const QColor &color) {
    const auto linear = [](double c) { return c <= .04045 ? c / 12.92 : std::pow((c + .055) / 1.055, 2.4); };
    return .2126 * linear(color.redF()) + .7152 * linear(color.greenF()) + .0722 * linear(color.blueF());
}

}

class ThemeTests : public QObject {
    Q_OBJECT
private slots:
    void alphaFirstHexKeepsRgb() {
        const auto color = parseColorToken("#e00e1e36");
        QCOMPARE(color.name(), QString("#0e1e36"));
        QCOMPARE(color.alpha(), 0xe0);
    }

    void plainHexIsOpaque() {
        const auto color = parseColorToken("#0E1E36");
        QCOMPARE(color.name(), QString("#0e1e36"));
        QCOMPARE(color.alpha(), 255);
    }

    void qtRgbaWithDivisions() {
        const auto color = parseColorToken("Qt.rgba(14 / 255, 30 / 255, 54 / 255, 0.88)");
        QCOMPARE(color.name(), QString("#0e1e36"));
        QCOMPARE(color.alpha(), 224);
    }

    void malformedTokensThrow() {
        for (const auto &text : {"", "#12", "#12345", "red", "Qt.rgba(1, 2, 3)", "Qt.rgba(2, 0, 0, 1)", "#gg0e1e36"})
            QVERIFY_THROWS_EXCEPTION(std::invalid_argument, parseColorToken(text));
    }

    void akaneBackgroundKeepsOpacityAndRgb() {
        QTemporaryDir dir;
        write(dir.filePath("Colors.qml"), colors("\"#e00e1e36\""));
        const auto theme = importPalette(dir.filePath("Colors.qml"));
        QCOMPARE(theme.hex("background"), QString("#0e1e36"));
        QCOMPARE(theme.backgroundAlpha, 0xe0);
        QCOMPARE(theme.backgroundWithAlpha().alpha(), 0xe0);
        QCOMPARE(theme.hex("surface"), QString("#0e1e36")); // backgroundAlt, case-insensitive
    }

    void qtRgbaBackgroundKeepsOpacity() {
        QTemporaryDir dir;
        write(dir.filePath("Colors.qml"), colors("Qt.rgba(14 / 255, 30 / 255, 54 / 255, 0.88)"));
        const auto theme = importPalette(dir.filePath("Colors.qml"));
        QCOMPARE(theme.hex("background"), QString("#0e1e36"));
        QCOMPARE(theme.backgroundAlpha, 224);
    }

    void opaqueBackgroundIsSolid() {
        QTemporaryDir dir;
        write(dir.filePath("Colors.qml"), colors("\"#181215\""));
        QCOMPARE(importPalette(dir.filePath("Colors.qml")).backgroundAlpha, 255);
    }

    void mappedThemeSurvivesSaveAndLoad() {
        QTemporaryDir dir;
        write(dir.filePath("Colors.qml"), colors("Qt.rgba(14 / 255, 30 / 255, 54 / 255, 0.88)"));
        const auto theme = importPalette(dir.filePath("Colors.qml"));
        saveTheme(dir.filePath("theme.json"), theme);
        QCOMPARE(loadTheme(dir.filePath("theme.json")), theme);
        QCOMPARE(normalized(theme), theme);
    }

    void tilesAreNearWhiteAndTextIsDark() {
        for (const auto &name : presets()) {
            const auto theme = preset(name);
            QVERIFY2(luminance(theme.color("tile")) > .8, qPrintable(name));
            QVERIFY2(luminance(theme.color("tile_text")) < .1, qPrintable(name));
        }
        QTemporaryDir dir;
        write(dir.filePath("Colors.qml"), colors("\"#e00e1e36\"", "\"#ff3366\""));
        const auto mapped = importPalette(dir.filePath("Colors.qml"));
        QVERIFY(luminance(mapped.color("tile")) > .8);
        QVERIFY(luminance(mapped.color("tile_text")) < .1);
        QVERIFY(mapped.color("tile").red() != 255 || mapped.color("tile").green() != 255); // tinted, not pure white
    }

    void setColorsAcceptsOpacity() {
        auto theme = setColors(preset("slate"), {"opacity=0.5", "accent=#ff0000"});
        QCOMPARE(theme.backgroundAlpha, 128);
        QCOMPARE(theme.hex("accent"), QString("#ff0000"));
        QCOMPARE(setColors(theme, {"opacity=25%"}).backgroundAlpha, 64);
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, setColors(theme, {"opacity=2"}));
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, setColors(theme, {"nope=#ffffff"}));
    }

    void invalidFileIsRejected() {
        QTemporaryDir dir;
        auto data = preset("slate").toJson();
        data.insert("background_alpha", 300);
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, parseTheme(data));
        data = preset("slate").toJson();
        auto palette = data.value("colors").toObject();
        palette.insert("text", "#12345");
        data.insert("colors", palette);
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, parseTheme(data));
    }

    void watcherFollowsSymlinkChainWithoutRewriteLoops() {
        QTemporaryDir dir;
        const QString themes = dir.filePath("themes");
        write(themes + "/alpha/Colors.qml", colors("\"#e00e1e36\"", "\"#9279aa\""));
        write(themes + "/beta/Colors.qml", colors("\"#181215\"", "\"#f7b2df\""));
        QVERIFY(QFile::link(themes + "/alpha", themes + "/current"));
        QVERIFY(QFile::link("current", themes + "/selector"));
        const QString output = dir.filePath("config/theme.json");
        auto bound = bindSource(preset("slate"), "desktop", themes + "/selector");
        saveTheme(output, bound);
        ThemeWatcher watcher(output, loadTheme(output));
        QSignalSpy changed(&watcher, &ThemeWatcher::changed);
        QTest::qWait(150);
        QCOMPARE(watcher.theme().backgroundAlpha, 0xe0);
        QCOMPARE(watcher.theme().hex("accent"), QString("#9279aa"));

        // Retarget the middle link atomically, as a theme switch does.
        QVERIFY(QFile::link("beta", themes + "/.next"));
        QCOMPARE(::rename(qPrintable(themes + "/.next"), qPrintable(themes + "/current")), 0);
        QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 1, 3000);
        QCOMPARE(watcher.theme().hex("accent"), QString("#f7b2df"));
        QCOMPARE(watcher.theme().backgroundAlpha, 255);
        QCOMPARE(loadTheme(output), watcher.theme());

        // Editing the file in place, and replacing it atomically, are both seen.
        const int before = changed.count();
        write(themes + "/beta/Colors.qml", colors("\"#181215\"", "\"#112233\""));
        QTRY_VERIFY_WITH_TIMEOUT(changed.count() > before, 3000);
        QCOMPARE(watcher.theme().hex("accent"), QString("#112233"));
        write(themes + "/beta/Colors.qml.tmp", colors("\"#181215\"", "\"#445566\""));
        QVERIFY(QFile::remove(themes + "/beta/Colors.qml"));
        QVERIFY(QFile::rename(themes + "/beta/Colors.qml.tmp", themes + "/beta/Colors.qml"));
        QTRY_COMPARE_WITH_TIMEOUT(watcher.theme().hex("accent"), QString("#445566"), 3000);

        // Settled: the saved file stops changing, so reloads cannot feed on their own output.
        QTest::qWait(300);
        const auto stamp = QFileInfo(output).lastModified();
        QTest::qWait(400);
        QCOMPARE(QFileInfo(output).lastModified(), stamp);
    }

    void invalidSourceKeepsLastTheme() {
        QTemporaryDir dir;
        const QString source = dir.filePath("palette/Colors.qml");
        write(source, colors("\"#181215\""));
        const QString output = dir.filePath("config/theme.json");
        saveTheme(output, bindSource(preset("slate"), "file", source));
        ThemeWatcher watcher(output, loadTheme(output));
        QSignalSpy rejected(&watcher, &ThemeWatcher::rejected);
        QTest::qWait(100);
        const auto before = watcher.theme();
        write(source, "this is not a palette");
        QTRY_VERIFY_WITH_TIMEOUT(rejected.count() >= 1, 3000);
        QCOMPARE(watcher.theme(), before);

        // Restoring the same palette recovers even though the colors never changed.
        QSignalSpy recovered(&watcher, &ThemeWatcher::recovered);
        write(source, colors("\"#181215\""));
        QTRY_VERIFY_WITH_TIMEOUT(recovered.count() >= 1, 3000);
        QCOMPARE(watcher.theme(), before);
    }
};

QTEST_GUILESS_MAIN(ThemeTests)
#include "theme_tests.moc"
