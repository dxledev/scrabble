/**
 * @file widgets_tests.cpp
 * @brief Qt Test coverage for the painted board, rack, tiles and icons.
 *
 * Widgets are rendered offscreen and checked by sampling pixels and by synthesized mouse clicks.
 * Icon tests confirm that every shape paints in the requested color and only changes RGB.
 */
#include "core/game.h"
#include "core/rules.h"
#include "core/theme.h"
#include "ui/board_view.h"
#include "ui/icons.h"
#include "ui/rack_view.h"

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QSignalSpy>
#include <QTest>
#include <algorithm>

using namespace scrabble;

namespace {

constexpr int kTolerance = 4;

bool closeTo(QRgb actual, const QColor &expected) {
    return std::abs(qRed(actual) - expected.red()) <= kTolerance
        && std::abs(qGreen(actual) - expected.green()) <= kTolerance
        && std::abs(qBlue(actual) - expected.blue()) <= kTolerance;
}

QRect cellRect(const QRectF &board, int row, int col) {
    const qreal cell = board.width() / kBoardSize;
    return QRect(qRound(board.left() + col * cell), qRound(board.top() + row * cell), qRound(cell), qRound(cell));
}

QPoint cellCenter(const QRectF &board, int row, int col) {
    return cellRect(board, row, col).center();
}

Board boardWith(int row, int col, char letter) {
    Board board{};
    cellAt(board, row, col) = Cell{letter, false};
    return board;
}

}

class WidgetsTests : public QObject {
    Q_OBJECT

private slots:
    void boardCenterDiffersFromEmpty_data();
    void boardCenterDiffersFromEmpty();
    void pendingTileUsesTileColor_data();
    void pendingTileUsesTileColor();
    void committedTileChangesRender_data();
    void committedTileChangesRender();
    void boardClicksEmitByState_data();
    void boardClicksEmitByState();
    void rackClicksSkipUsedSlots_data();
    void rackClicksSkipUsedSlots();
    void rackRendersTilesAndHidden_data();
    void rackRendersTilesAndHidden();
    void iconsPaintRequestedColor_data();
    void iconsPaintRequestedColor();
    void iconsRecolorWithoutChangingAlpha_data();
    void iconsRecolorWithoutChangingAlpha();
};

void WidgetsTests::boardCenterDiffersFromEmpty_data() {
    QTest::addColumn<QString>("themeName");
    QTest::newRow("slate") << "slate";
    QTest::newRow("paper") << "paper";
}

void WidgetsTests::boardCenterDiffersFromEmpty() {
    QFETCH(QString, themeName);
    const auto theme = preset(themeName);
    BoardView view;
    view.setTheme(theme);
    view.resize(640, 640);
    const QImage image = view.grab().toImage();
    QVERIFY(!image.isNull());

    const QRectF board = view.boardRect();
    QVERIFY(board.width() > 0);
    const QRect center = cellRect(board, kBoardSize / 2, kBoardSize / 2);
    const QRect plain = cellRect(board, 1, 2);
    QCOMPARE(premiumAt(1, 2), Premium::None);
    const QPoint centerSample = center.topLeft() + QPoint(center.width() / 6, center.height() / 6);
    const QPoint plainSample = plain.topLeft() + QPoint(plain.width() / 6, plain.height() / 6);
    QVERIFY(image.pixel(centerSample) != image.pixel(plainSample));
    QVERIFY(image.pixel(plainSample) == QColor(theme.color("surface_alt")).rgb());
}

void WidgetsTests::pendingTileUsesTileColor_data() {
    QTest::addColumn<QString>("themeName");
    QTest::newRow("slate") << "slate";
    QTest::newRow("paper") << "paper";
}

void WidgetsTests::pendingTileUsesTileColor() {
    QFETCH(QString, themeName);
    const auto theme = preset(themeName);
    BoardView view;
    view.setTheme(theme);
    view.resize(640, 640);
    view.setPending({Placement{3, 4, 'Q', false}});
    const QImage image = view.grab().toImage();
    const QRect tile = cellRect(view.boardRect(), 3, 4);
    const QPoint sample = tile.topLeft() + QPoint(qRound(tile.width() * 0.2), qRound(tile.height() * 0.2));
    QVERIFY(closeTo(image.pixel(sample), theme.color("tile")));
}

void WidgetsTests::committedTileChangesRender_data() {
    QTest::addColumn<QString>("themeName");
    QTest::newRow("slate") << "slate";
    QTest::newRow("paper") << "paper";
}

void WidgetsTests::committedTileChangesRender() {
    QFETCH(QString, themeName);
    BoardView view;
    view.setTheme(preset(themeName));
    view.resize(640, 640);
    const QImage before = view.grab().toImage();
    view.setBoard(boardWith(7, 7, 'A'));
    const QImage after = view.grab().toImage();
    QVERIFY(before != after);
}

void WidgetsTests::boardClicksEmitByState_data() {
    QTest::addColumn<int>("row");
    QTest::addColumn<int>("col");
    QTest::addColumn<bool>("pending");
    QTest::newRow("pending tile") << 3 << 4 << true;
    QTest::newRow("empty square") << 0 << 0 << false;
    QTest::newRow("committed tile") << 7 << 7 << false;
}

void WidgetsTests::boardClicksEmitByState() {
    QFETCH(int, row);
    QFETCH(int, col);
    QFETCH(bool, pending);
    BoardView view;
    view.setTheme(preset("slate"));
    view.resize(640, 640);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    view.setPending({Placement{3, 4, 'Q', false}});
    view.setBoard(boardWith(7, 7, 'A'));

    QSignalSpy pendingSpy(&view, &BoardView::pendingClicked);
    QSignalSpy squareSpy(&view, &BoardView::squareClicked);
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, cellCenter(view.boardRect(), row, col));
    if (pending) {
        QCOMPARE(pendingSpy.count(), 1);
        QCOMPARE(squareSpy.count(), 0);
        QCOMPARE(pendingSpy.at(0).at(0).toInt(), row);
        QCOMPARE(pendingSpy.at(0).at(1).toInt(), col);
    } else {
        QCOMPARE(pendingSpy.count(), 0);
        QCOMPARE(squareSpy.count(), 1);
        QCOMPARE(squareSpy.at(0).at(0).toInt(), row);
        QCOMPARE(squareSpy.at(0).at(1).toInt(), col);
    }

    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, QPoint(1, 1));
    QCOMPARE(pendingSpy.count() + squareSpy.count(), 1);
}

void WidgetsTests::rackClicksSkipUsedSlots_data() {
    QTest::addColumn<int>("index");
    QTest::addColumn<bool>("emits");
    QTest::newRow("first slot") << 0 << true;
    QTest::newRow("used slot") << 2 << false;
    QTest::newRow("last slot") << 6 << true;
}

void WidgetsTests::rackClicksSkipUsedSlots() {
    QFETCH(int, index);
    QFETCH(bool, emits);
    RackView rack;
    rack.setTheme(preset("paper"));
    rack.resize(420, 64);
    rack.show();
    QVERIFY(QTest::qWaitForWindowExposed(&rack));
    rack.setTiles({'A', 'B', 'C', 'D', 'E', 'F', 'G'}, {false, false, true, false, false, false, false});

    // Mirrors the slot layout in RackView::slotRect: side = min(h, w / 7.6), gap = side * 0.08, centered.
    const qreal side = std::min<qreal>(rack.height(), rack.width() / 7.6);
    const qreal gap = side * 0.08;
    const qreal total = kRackSize * side + (kRackSize - 1) * gap;
    const qreal left = (rack.width() - total) / 2;
    const QPoint center(qRound(left + index * (side + gap) + side / 2), rack.height() / 2);

    QSignalSpy spy(&rack, &RackView::tileClicked);
    QTest::mouseClick(&rack, Qt::LeftButton, Qt::NoModifier, center);
    QCOMPARE(spy.count(), emits ? 1 : 0);
    if (emits)
        QCOMPARE(spy.at(0).at(0).toInt(), index);
}

void WidgetsTests::rackRendersTilesAndHidden_data() {
    QTest::addColumn<QString>("themeName");
    QTest::newRow("slate") << "slate";
    QTest::newRow("paper") << "paper";
}

void WidgetsTests::rackRendersTilesAndHidden() {
    QFETCH(QString, themeName);
    const auto theme = preset(themeName);
    RackView rack;
    rack.setTheme(theme);
    rack.resize(420, 64);
    rack.setTiles({'A', 'B', 'C', 'D', 'E', 'F', 'G'}, {false, false, true, false, false, false, false});
    rack.setSelected(1);
    const QImage image = rack.grab().toImage();
    QVERIFY(!image.isNull());

    // Slot 0 is unused and unselected, so its interior shows the plain tile color.
    const qreal side = std::min<qreal>(rack.height(), rack.width() / 7.6);
    const qreal gap = side * 0.08;
    const qreal total = kRackSize * side + (kRackSize - 1) * gap;
    const qreal left = (rack.width() - total) / 2;
    const QPoint sample(qRound(left + side * 0.2), rack.height() / 2);
    QVERIFY(closeTo(image.pixel(sample), theme.color("tile")));

    rack.setFaceDown(true);
    const QImage hidden = rack.grab().toImage();
    QVERIFY(!hidden.isNull());
    QVERIFY(hidden != image);
}

void WidgetsTests::iconsPaintRequestedColor_data() {
    QTest::addColumn<int>("icon");
    for (int icon = static_cast<int>(Icon::Play); icon <= static_cast<int>(Icon::Stats); ++icon)
        QTest::newRow(qPrintable(QString::number(icon))) << icon;
}

void WidgetsTests::iconsPaintRequestedColor() {
    QFETCH(int, icon);
    const QColor color(200, 30, 90);
    const QIcon qicon = makeIcon(static_cast<Icon>(icon), color, 64, 1.0);
    QVERIFY(!qicon.isNull());
    const QPixmap pixmap = qicon.pixmap(64, 64);
    QVERIFY(!pixmap.isNull());
    const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);

    int painted = 0;
    bool exact = false;
    for (int y = 16; y < 48; ++y) {
        for (int x = 16; x < 48; ++x) {
            const QRgb pixel = image.pixel(x, y);
            if (qAlpha(pixel) == 0)
                continue;
            ++painted;
            if (qAlpha(pixel) == 255 && closeTo(pixel, color))
                exact = true;
        }
    }
    QVERIFY2(painted > 20, "icon has too few painted pixels in its center");
    QVERIFY2(exact, "icon has no pixel in the requested color");
}

void WidgetsTests::iconsRecolorWithoutChangingAlpha_data() {
    iconsPaintRequestedColor_data();
}

void WidgetsTests::iconsRecolorWithoutChangingAlpha() {
    QFETCH(int, icon);
    const QColor first(200, 30, 90);
    const QColor second(20, 140, 250);
    const auto render = [icon](const QColor &color) {
        QImage image(64, 64, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        drawIcon(painter, static_cast<Icon>(icon), QRectF(0, 0, 64, 64), color);
        painter.end();
        return image.convertToFormat(QImage::Format_ARGB32);
    };
    const QImage a = render(first);
    const QImage b = render(second);
    bool anyChange = false;
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const QRgb pa = a.pixel(x, y);
            const QRgb pb = b.pixel(x, y);
            QCOMPARE(qAlpha(pa), qAlpha(pb));
            if (qAlpha(pa) == 255) {
                QVERIFY(closeTo(pa, first));
                QVERIFY(closeTo(pb, second));
            }
            if (pa != pb)
                anyChange = true;
        }
    }
    QVERIFY(anyChange);
}

QTEST_MAIN(WidgetsTests)
#include "widgets_tests.moc"
