/**
 * @file window_tests.cpp
 * @brief Window-level behavior: in-window modals, a full turn, live recoloring, and opacity.
 */
#include "core/game.h"
#include "core/storage.h"
#include "core/theme.h"
#include "ui/window.h"

#include <QApplication>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

using namespace scrabble;

namespace {

int topLevelCount() {
    int count = 0;
    for (auto *widget : QApplication::topLevelWidgets())
        count += widget->isVisible() ? 1 : 0;
    return count;
}

std::optional<Game> savedGame(const QString &directory) {
    return Game::fromJson(readJson(directory + "/game.json").value("game").toObject());
}

}

class WindowTests : public QObject {
    Q_OBJECT
    QTemporaryDir dir_;
    QString config() const { return dir_.path(); }

private slots:
    void init() {
        QDir(config()).removeRecursively();
        QDir().mkpath(config());
        saveTheme(config() + "/theme.json", preset("slate"));
    }

    void modalsStayInsideTheWindow() {
        Window window(config(), preset("slate"), std::nullopt, 7);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QCOMPARE(topLevelCount(), 1);
        for (const auto key : {Qt::Key_F1}) {
            QTest::keyClick(&window, key);
            QCOMPARE(topLevelCount(), 1);
            QTest::keyClick(&window, Qt::Key_Escape);
        }
        for (const auto &chord : {Qt::Key_N, Qt::Key_I, Qt::Key_P, Qt::Key_E}) {
            QTest::keyClick(&window, chord, Qt::ControlModifier);
            QCOMPARE(topLevelCount(), 1);
            auto *modal = window.findChild<QWidget *>("modal");
            QVERIFY(modal && modal->isVisible());
            QTest::keyClick(modal, Qt::Key_Escape);
            QVERIFY(!modal->isVisible());
        }
    }

    void playingAWordGetsAComputerReply() {
        {
            Window window(config(), preset("slate"), QString("hard"), 7);
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            for (const char letter : QByteArray("GROUT"))
                QTest::keyClick(&window, Qt::Key(Qt::Key_A + letter - 'A'));
            QTest::keyClick(&window, Qt::Key_Return);
            QTRY_VERIFY_WITH_TIMEOUT(savedGame(config()) && savedGame(config())->log().size() == 2, 6000);
            const auto game = *savedGame(config());
            QCOMPARE(game.log()[0].kind, TurnKind::Play);
            QCOMPARE(game.log()[0].text, std::string("GROUT"));
            QCOMPARE(game.score(Game::kHuman), 14);
            QVERIFY(game.score(Game::kComputer) > 0);
            QCOMPARE(game.turn(), Game::kHuman);
        }
        // The game resumes after a restart.
        Window again(config(), preset("slate"), std::nullopt, std::nullopt);
        QCOMPARE(savedGame(config())->log().size(), size_t(2));
    }

    void tabShufflesWithoutStealingKeyboardFocus() {
        Window window(config(), preset("slate"), std::nullopt, 7);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::keyClick(&window, Qt::Key_Tab);
        QVERIFY2(QApplication::focusWidget() == &window, "Tab moved focus away from the board");
        QTest::keyClick(&window, Qt::Key_Space); // vertical
        for (const char letter : QByteArray("GROUT"))
            QTest::keyClick(&window, Qt::Key(Qt::Key_A + letter - 'A'));
        QTest::keyClick(&window, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(savedGame(config()) && !savedGame(config())->log().empty(), 3000);
        const auto game = *savedGame(config());
        QCOMPARE(game.log()[0].text, std::string("GROUT"));
        const char expected[] = {'G', 'R', 'O', 'U', 'T'};
        for (int row = 0; row < 5; ++row)
            QCOMPARE(cellAt(game.board(), 7 + row, 7).letter, expected[row]);
    }

    void invalidWordIsRejectedAndKeptPending() {
        Window window(config(), preset("slate"), std::nullopt, 7);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        for (const char letter : QByteArray("RT"))
            QTest::keyClick(&window, Qt::Key(Qt::Key_A + letter - 'A'));
        QTest::keyClick(&window, Qt::Key_Return);
        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(savedGame(config())->log().size(), size_t(0));
        QCOMPARE(savedGame(config())->score(Game::kHuman), 0);
    }

    void recolorsLiveAndAppliesOpacity() {
        Window window(config(), preset("slate"), std::nullopt, 7);
        window.resize(700, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto sample = [&] { return window.grab().toImage().convertToFormat(QImage::Format_ARGB32).pixelColor(2, 2); };
        QCOMPARE(sample().name(), preset("slate").hex("background"));

        saveTheme(config() + "/theme.json", preset("paper"));
        QTRY_COMPARE_WITH_TIMEOUT(sample().name(), preset("paper").hex("background"), 3000);

        auto translucent = setColors(preset("paper"), {"opacity=0.5"});
        saveTheme(config() + "/theme.json", translucent);
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(sample().alpha() - 128) <= 1, 3000);
        // Premultiplied storage may round a channel by one.
        const QColor expected = preset("paper").color("background");
        QVERIFY(qAbs(sample().red() - expected.red()) <= 2 && qAbs(sample().green() - expected.green()) <= 2
                && qAbs(sample().blue() - expected.blue()) <= 2);
    }
};

QTEST_MAIN(WindowTests)
#include "window_tests.moc"
