/**
 * @file tile_painter.cpp
 * @brief Solid-fill tile rendering with state outlines and face values.
 *
 * Every tile is a rounded rectangle filled with the theme "tile" color and an edge blended toward
 * "tile_text". State decorations are plain solid strokes or squares, so no gradients are involved.
 */
#include "tile_painter.h"
#include "core/rules.h"

#include <QFont>
#include <QPen>
#include <array>

namespace scrabble {
namespace {

QColor mix(const QColor &from, const QColor &to, qreal amount) {
    const auto blend = [amount](int a, int b) { return qRound(a + (b - a) * amount); };
    return QColor(blend(from.red(), to.red()), blend(from.green(), to.green()), blend(from.blue(), to.blue()));
}

/** @brief Cached single-letter strings so painting a tile does not allocate. */
const QString &letterText(char letter) {
    static const std::array<QString, 26> letters = [] {
        std::array<QString, 26> table;
        for (int i = 0; i < 26; ++i)
            table[i] = QString(QChar('A' + i));
        return table;
    }();
    return letters[letter - 'A'];
}

/** @brief Cached face-value strings for the values used by the standard tile set. */
QString valueText(int value) {
    static const std::array<QString, 11> values = [] {
        std::array<QString, 11> table;
        for (int i = 0; i < 11; ++i)
            table[i] = QString::number(i);
        return table;
    }();
    return value >= 0 && value < 11 ? values[value] : QString::number(value);
}

}

void drawTile(QPainter &painter, const QRectF &rect, char letter, bool blank, const Theme &theme,
              TileState state) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    if (state == TileState::Ghost)
        painter.setOpacity(0.45);

    const QColor tile = theme.color(QStringLiteral("tile"));
    const QColor text = theme.color(QStringLiteral("tile_text"));
    const QColor accent = theme.color(QStringLiteral("accent"));
    const QColor edge = mix(tile, text, 0.28);
    const qreal side = qMin(rect.width(), rect.height());
    const qreal radius = 0.12 * side;

    if (state == TileState::Selected) {
        QColor shadow = text;
        shadow.setAlpha(64);
        painter.setPen(Qt::NoPen);
        painter.setBrush(shadow);
        painter.drawRoundedRect(rect, radius, radius);
    }
    const QRectF body = state == TileState::Selected ? rect.translated(0, -2) : rect;

    painter.setPen(Qt::NoPen);
    painter.setBrush(tile);
    painter.drawRoundedRect(body, radius, radius);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(edge, 1));
    painter.drawRoundedRect(body.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);

    if (letter == '?') {
        painter.setPen(QPen(edge, qMax(1.0, side * 0.07)));
        painter.drawEllipse(body.center(), side * 0.2, side * 0.2);
    } else {
        QFont font = painter.font();
        font.setBold(true);
        const bool standard = letter >= 'A' && letter <= 'Z';
        font.setPixelSize(qMax(1, qRound(side * 0.55)));
        painter.setFont(font);
        painter.setPen(blank ? mix(text, tile, 0.6) : text);
        if (standard)
            painter.drawText(body, Qt::AlignCenter, letterText(letter));
        else
            painter.drawText(body, Qt::AlignCenter, QString(QChar(letter)));
        if (standard && !blank) {
            const int value = letterValue(letter);
            font.setPixelSize(qMax(1, qRound(side * 0.24)));
            painter.setFont(font);
            painter.setPen(text);
            const qreal margin = side * 0.08;
            painter.drawText(body.adjusted(0, 0, -margin, -margin * 0.6), Qt::AlignRight | Qt::AlignBottom,
                             valueText(value));
        }
    }

    switch (state) {
    case TileState::Pending:
        painter.setPen(QPen(accent, 2));
        painter.drawRoundedRect(body.adjusted(1, 1, -1, -1), radius, radius);
        break;
    case TileState::LastPlay: {
        painter.setPen(QPen(accent, 1));
        painter.drawRoundedRect(body.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
        const qreal mark = side * 0.14;
        const qreal inset = side * 0.12;
        painter.setPen(Qt::NoPen);
        painter.setBrush(accent);
        painter.drawRect(QRectF(body.right() - inset - mark, body.top() + inset, mark, mark));
        break;
    }
    case TileState::Selected:
        painter.setPen(QPen(accent, 3));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(body.adjusted(1.5, 1.5, -1.5, -1.5), radius, radius);
        break;
    case TileState::Normal:
    case TileState::Ghost:
        break;
    }
    painter.restore();
}

}
