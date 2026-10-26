/**
 * @file window_tests.cpp
 * @brief Window-level behavior: in-window modals, a full turn, live recoloring, and opacity.
 */
#include "core/game.h"
#include "core/storage.h"
#include "core/theme.h"
#include "ui/board_view.h"
#include "ui/modal_backdrop.h"
#include "ui/window.h"

#include <QLabel>
#include <QPushButton>

#include <QApplication>
#include <QImage>
#include <QJsonObject>
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
            auto *rules = window.findChild<QWidget *>("modal");
            QTest::keyClick(rules, Qt::Key_Escape);
            QTRY_VERIFY(!rules->isVisible());
        }
        for (const auto &chord : {Qt::Key_N, Qt::Key_I, Qt::Key_P, Qt::Key_E}) {
            QTest::keyClick(&window, chord, Qt::ControlModifier);
            QCOMPARE(topLevelCount(), 1);
            auto *modal = window.findChild<QWidget *>("modal");
            QVERIFY(modal && modal->isVisible());
            QTest::keyClick(modal, Qt::Key_Escape);
            QTRY_VERIFY(!modal->isVisible());
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

    void modalTextIsNeverClipped() {
        Window window(config(), preset("slate"), std::nullopt, 7);
        window.resize(1000, 900);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *modal = window.findChild<QWidget *>("modal");
        for (const auto key : {Qt::Key_F1, Qt::Key_I, Qt::Key_N, Qt::Key_P}) {
            QTest::keyClick(&window, key, key == Qt::Key_F1 ? Qt::NoModifier : Qt::ControlModifier);
            QVERIFY(modal->isVisible());
            int checked = 0;
            for (auto *label : modal->findChildren<QLabel *>()) {
                if (!label->wordWrap() || label->text().isEmpty())
                    continue;
                const QRect needed = label->fontMetrics().boundingRect(
                    QRect(0, 0, label->width(), 100000), Qt::TextWordWrap | Qt::AlignLeft, label->text());
                QVERIFY2(label->height() >= needed.height(), qPrintable(label->text().left(30)));
                ++checked;
            }
            QVERIFY(checked > 0 || key == Qt::Key_I);
            QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
            QTRY_VERIFY(!modal->isVisible());
        }
    }

    void modalFadesInAndOutWithoutFlashing() {
        Window window(config(), preset("slate"), std::nullopt, 7);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *modal = window.findChild<ModalBackdrop *>("modal");
        QTest::keyClick(&window, Qt::Key_F1);
        QVERIFY(modal->isVisible());
        // The first frame is fully transparent, so the live window shows through unchanged.
        QVERIFY(modal->progress() < 1);
        QTRY_COMPARE(modal->progress(), 1.0);
        QTest::keyClick(modal, Qt::Key_Escape);
        QVERIFY(modal->isVisible()); // still fading out
        QTRY_VERIFY(!modal->isVisible());
        QCOMPARE(modal->progress(), 0.0);
    }

    void modalButtonCloseAnimates() {
        Window window(config(), preset("slate"), std::nullopt, 7);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *modal = window.findChild<ModalBackdrop *>("modal");
        for (const auto key : {Qt::Key_F1, Qt::Key_I}) {
            QTest::keyClick(&window, key, key == Qt::Key_F1 ? Qt::NoModifier : Qt::ControlModifier);
            QTRY_COMPARE(modal->progress(), 1.0);
            QList<qreal> seen;
            QObject::connect(modal, &ModalBackdrop::progressChanged, modal, [&](qreal p) { seen.append(p); });
            auto buttons = modal->findChildren<QPushButton *>();
            QVERIFY(!buttons.isEmpty());
            buttons.last()->click();
            QVERIFY(modal->isVisible());
            QTRY_VERIFY(!modal->isVisible());
            QVERIFY2(seen.size() > 2, qPrintable(QString::number(seen.size())));
            modal->disconnect(modal, &ModalBackdrop::progressChanged, nullptr, nullptr);
        }
    }

    void modalEntryAndExitNeverJumpVisually() {
        Window window(config(), preset("slate"), std::nullopt, 7);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *modal = window.findChild<ModalBackdrop *>("modal");
        auto difference = [](const QImage &a, const QImage &b) {
            long long total = 0;
            for (int y = 0; y < a.height(); ++y)
                for (int x = 0; x < a.width(); ++x)
                    total += qAbs(qGray(a.pixel(x, y)) - qGray(b.pixel(x, y)));
            return total / double(a.width() * a.height());
        };
        window.setFocus();
        QTest::qWait(100); // let queued layout and panel updates settle
        const QImage before = window.grab().toImage();
        QList<double> frames;
        QObject::connect(modal, &ModalBackdrop::progressChanged, modal, [&] {
            frames.append(difference(before, window.grab().toImage()));
        });
        QTest::keyClick(&window, Qt::Key_F1);
        // The frame right after opening is identical to the window beneath.
        const double first = difference(before, window.grab().toImage());
        QVERIFY(first < 0.5);
        QTRY_COMPARE(modal->progress(), 1.0);
        const int opened = frames.size();
        QVERIFY(opened > 1);
        for (int i = 1; i < opened; ++i)
            QVERIFY2(frames[i] >= frames[i - 1] - 0.5, "opening must only move away from the window");
        const double full = frames.last();
        QVERIFY(full > 1);
        for (int i = 1; i < opened; ++i)
            QVERIFY2(frames[i] - frames[i - 1] < full * 0.85, "no single-frame jump while opening");
        QTest::keyClick(modal, Qt::Key_Escape);
        QTRY_VERIFY(!modal->isVisible());
        QVERIFY2(frames.size() - opened > 2, qPrintable(QString::number(frames.size() - opened)));
        for (int i = opened + 1; i < frames.size(); ++i)
            QVERIFY2(frames[i] <= frames[i - 1] + 0.5, "closing must only move back to the window");
        QVERIFY(frames.last() < full * 0.85);
        QVERIFY(difference(before, window.grab().toImage()) < 1.0);
    }

    void boardIsCenteredAndNeverSqueezed_data() {
        QTest::addColumn<int>("width");
        QTest::addColumn<int>("height");
        QTest::newRow("minimum") << 620 << 760;
        QTest::newRow("narrow") << 700 << 900;
        QTest::newRow("tiled half screen") << 1005 << 1085; // a real compositor-assigned size
        QTest::newRow("square-ish") << 1000 << 900;
        QTest::newRow("wide") << 1400 << 900;
        QTest::newRow("ultrawide") << 2000 << 1100;
    }

    void boardIsCenteredAndNeverSqueezed() {
        QFETCH(int, width);
        QFETCH(int, height);
        Window window(config(), preset("slate"), std::nullopt, 7);
        window.resize(width, height);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *board = window.findChild<BoardView *>();
        const double center = board->mapTo(&window, QPoint(0, 0)).x() + board->boardRect().center().x();
        QVERIFY2(qAbs(center - window.width() / 2.0) <= 2.0, "board is off-center");
        // The board must keep the full height the window gives it, never shrink for the side panels.
        const double side = board->boardRect().width();
        QVERIFY2(side >= qMin(board->width(), board->height()) - 40, "board was squeezed by a panel");
        // Whenever the move list is shown, the board must still be at least as wide as it is tall.
        QTRY_VERIFY_WITH_TIMEOUT(!window.findChild<QWidget *>("sidePanel")->isVisible()
                                     || board->width() >= board->height() - 2, 2000);
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

    void hintsStartByLevelAndSpendOncePerPreview_data() {
        QTest::addColumn<QString>("level");
        QTest::addColumn<int>("start");
        QTest::newRow("easy") << "easy" << 3;
        QTest::newRow("medium") << "medium" << 2;
        QTest::newRow("hard") << "hard" << 1;
    }

    void hintsStartByLevelAndSpendOncePerPreview() {
        QFETCH(QString, level);
        QFETCH(int, start);
        Window window(config(), preset("slate"), level, 7);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto hintsLeft = [&] { return readJson(config() + "/game.json").value("hints_left").toInt(-1); };
        QCOMPARE(hintsLeft(), start);
        QTest::keyClick(&window, Qt::Key_H, Qt::ControlModifier);
        QCOMPARE(hintsLeft(), start - 1);
        QTest::keyClick(&window, Qt::Key_H, Qt::ControlModifier); // same preview: free
        QCOMPARE(hintsLeft(), start - 1);
        // Recall and ask again until the allowance runs out; a spent bank refuses without charging.
        for (int left = start - 1; left > 0; --left) {
            QTest::keyClick(&window, Qt::Key_Escape);
            QTest::keyClick(&window, Qt::Key_H, Qt::ControlModifier);
            QCOMPARE(hintsLeft(), left - 1);
        }
        QTest::keyClick(&window, Qt::Key_Escape);
        QTest::keyClick(&window, Qt::Key_H, Qt::ControlModifier);
        QCOMPARE(hintsLeft(), 0);
    }

    void playingTheFinalWordOfARunEarnsAHint() {
        QJsonObject save{{"version", 1}, {"level", "easy"}, {"best_play", 0}, {"hints", 0}, {"stats_recorded", false},
                         {"hints_left", 0}, {"hint_progress", 4}, {"game", Game(7).toJson()}};
        writeJson(config() + "/game.json", save);
        Window window(config(), preset("slate"), std::nullopt, std::nullopt);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        for (const char letter : QByteArray("GROUT"))
            QTest::keyClick(&window, Qt::Key(Qt::Key_A + letter - 'A'));
        QTest::keyClick(&window, Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(readJson(config() + "/game.json").value("hints_left").toInt(-1), 1, 3000);
        QCOMPARE(readJson(config() + "/game.json").value("hint_progress").toInt(-1), 0);
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

// A fractional display scale exercises text measurement that integer scales hide.
int main(int argc, char **argv) {
    if (qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR"))
        qputenv("QT_SCALE_FACTOR", "1.5");
    QApplication app(argc, argv);
    WindowTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "window_tests.moc"
