/**
 * @file board_view.cpp
 * @brief Single-surface painting and hit testing for the 15 x 15 board.
 *
 * Cells are whole pixels so grid lines stay crisp at any size. Layers are painted in order:
 * squares, committed tiles, last-play and pending tiles, then the keyboard cursor.
 */
#include "board_view.h"
#include "tile_painter.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace scrabble {
namespace {

QColor mix(const QColor &from, const QColor &to, qreal amount) {
    const auto blend = [amount](int a, int b) { return qRound(a + (b - a) * amount); };
    return QColor(blend(from.red(), to.red()), blend(from.green(), to.green()), blend(from.blue(), to.blue()));
}

constexpr int kMargin = 6;


const Placement *findAt(const std::vector<Placement> &tiles, int row, int col) {
    for (const auto &tile : tiles) {
        if (tile.row == row && tile.col == col)
            return &tile;
    }
    return nullptr;
}

}

BoardView::BoardView(QWidget *parent) : QWidget(parent), theme_(preset("slate")) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void BoardView::setTheme(const Theme &theme) {
    theme_ = theme;
    update();
}

void BoardView::setBoard(const Board &board) {
    board_ = board;
    update();
}

void BoardView::setPending(const std::vector<Placement> &pending) {
    pending_ = pending;
    update();
}

void BoardView::setLastPlay(const std::vector<Placement> &tiles) {
    lastPlay_ = tiles;
    update();
}

void BoardView::setKeyboardCursor(int row, int col, bool horizontal) {
    cursorRow_ = row;
    cursorCol_ = col;
    cursorHorizontal_ = horizontal;
    update();
}

QRectF BoardView::boardRect() const {
    const int available = std::min(width(), height()) - 2 * kMargin;
    const int cell = std::max(1, available / kBoardSize);
    const qreal side = static_cast<qreal>(cell * kBoardSize);
    return QRectF((width() - side) / 2, (height() - side) / 2, side, side);
}

bool BoardView::squareAt(const QPointF &position, int &row, int &col) const {
    const QRectF board = boardRect();
    if (!board.contains(position))
        return false;
    const qreal cell = board.width() / kBoardSize;
    col = std::min(kBoardSize - 1, static_cast<int>((position.x() - board.left()) / cell));
    row = std::min(kBoardSize - 1, static_cast<int>((position.y() - board.top()) / cell));
    return true;
}

void BoardView::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    const QRectF board = boardRect();
    const qreal cell = board.width() / kBoardSize;
    const QColor surface = theme_.color(QStringLiteral("surface_alt"));
    const QColor text = theme_.color(QStringLiteral("text"));
    const QColor gridLine = mix(surface, theme_.color(QStringLiteral("grid")), 0.6);

    static const std::array<QString, 5> keys{{
        QString(), QStringLiteral("double_letter"), QStringLiteral("triple_letter"),
        QStringLiteral("double_word"), QStringLiteral("triple_word")}};
    static const std::array<QString, 5> labels{{
        QString(), QStringLiteral("2L"), QStringLiteral("3L"), QStringLiteral("2W"), QStringLiteral("3W")}};
    std::array<QColor, 5> fills;
    fills[0] = surface;
    for (std::size_t i = 1; i < keys.size(); ++i)
        fills[i] = mix(surface, theme_.color(keys[i]), 0.55);

    QFont labelFont = painter.font();
    labelFont.setBold(true);
    labelFont.setPixelSize(qMax(1, qRound(cell * 0.28)));

    painter.fillRect(board.adjusted(-1, -1, 0, 0), gridLine);
    painter.setFont(labelFont);
    painter.setPen(text);
    for (int row = 0; row < kBoardSize; ++row) {
        for (int col = 0; col < kBoardSize; ++col) {
            const QRectF square(board.left() + col * cell, board.top() + row * cell, cell - 1, cell - 1);
            const auto premium = static_cast<std::size_t>(premiumAt(row, col));
            painter.fillRect(square, fills[premium]);
            if (premium == 0)
                continue;
            if (row == kBoardSize / 2 && col == kBoardSize / 2) {
                QPainterPath star;
                const QPointF center = square.center();
                for (int point = 0; point < 10; ++point) {
                    const qreal radius = point % 2 == 0 ? cell * 0.32 : cell * 0.13;
                    const qreal angle = -std::numbers::pi / 2 + point * std::numbers::pi / 5;
                    const QPointF vertex(center.x() + radius * std::cos(angle), center.y() + radius * std::sin(angle));
                    if (point == 0)
                        star.moveTo(vertex);
                    else
                        star.lineTo(vertex);
                }
                star.closeSubpath();
                painter.setPen(Qt::NoPen);
                painter.setBrush(text);
                painter.drawPath(star);
                painter.setBrush(Qt::NoBrush);
                painter.setPen(text);
            } else {
                painter.drawText(square, Qt::AlignCenter, labels[premium]);
            }
        }
    }

    const auto squareRect = [&](int row, int col) {
        return QRectF(board.left() + col * cell, board.top() + row * cell, cell - 1, cell - 1);
    };
    const auto tileRect = [&](int row, int col) {
        return squareRect(row, col).adjusted(cell * 0.04, cell * 0.04, -cell * 0.04, -cell * 0.04);
    };

    for (int row = 0; row < kBoardSize; ++row) {
        for (int col = 0; col < kBoardSize; ++col) {
            const Cell &cellState = cellAt(board_, row, col);
            if (cellState.empty() || findAt(pending_, row, col))
                continue;
            const bool last = findAt(lastPlay_, row, col) != nullptr;
            drawTile(painter, tileRect(row, col), cellState.letter, cellState.blank, theme_,
                     last ? TileState::LastPlay : TileState::Normal);
        }
    }
    for (const auto &tile : pending_) {
        if (tile.row < 0 || tile.row >= kBoardSize || tile.col < 0 || tile.col >= kBoardSize)
            continue;
        drawTile(painter, tileRect(tile.row, tile.col), tile.letter, tile.blank, theme_, TileState::Pending);
    }

    if (cursorRow_ >= 0 && cursorCol_ >= 0) {
        const QRectF square = squareRect(cursorRow_, cursorCol_);
        const QColor accent = theme_.color(QStringLiteral("accent"));
        painter.setPen(QPen(accent, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(square.adjusted(2, 2, -2, -2));
        QPolygonF arrow;
        if (cursorHorizontal_) {
            arrow << QPointF(square.right() - cell * 0.24, square.center().y() - cell * 0.14)
                  << QPointF(square.right() - cell * 0.24, square.center().y() + cell * 0.14)
                  << QPointF(square.right() - cell * 0.06, square.center().y());
        } else {
            arrow << QPointF(square.center().x() - cell * 0.14, square.bottom() - cell * 0.24)
                  << QPointF(square.center().x() + cell * 0.14, square.bottom() - cell * 0.24)
                  << QPointF(square.center().x(), square.bottom() - cell * 0.06);
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(accent);
        painter.drawPolygon(arrow);
    }
}

void BoardView::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    int row = 0;
    int col = 0;
    if (!squareAt(event->position(), row, col))
        return;
    if (findAt(pending_, row, col))
        emit pendingClicked(row, col);
    else
        emit squareClicked(row, col);
}

}
