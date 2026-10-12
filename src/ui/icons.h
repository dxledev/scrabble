#pragma once

#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QRectF>

namespace scrabble {

enum class Icon { Play, Recall, Shuffle, Pass, Exchange, Hint, NewGame, Close, Rules, Check, Stats };

/**
 * @brief Draw an icon as stroked/filled vector paths in a single @p color on a square @p rect.
 *
 * Icons are pure QPainterPath shapes (round caps and joins, stroke width about rect/12), so they
 * recolor with the theme and never use bitmaps, gradients, emoji, or QIcon::fromTheme.
 */
void drawIcon(QPainter &painter, Icon icon, const QRectF &rect, const QColor &color);
/** @brief Render an icon into a QIcon of @p size logical pixels at the given color (device-pixel-ratio aware). */
QIcon makeIcon(Icon icon, const QColor &color, int size, qreal devicePixelRatio = 1.0);

}
