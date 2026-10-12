/**
 * @file icons.cpp
 * @brief Vector toolbar and dialog icons built from stroked and filled paths.
 *
 * Shapes are authored on a unit square and scaled to the target rectangle. Strokes are a twelfth
 * of the side with round caps and joins, so every icon takes its color from the caller alone.
 */
#include "icons.h"

#include <QPainterPath>
#include <QPixmap>

namespace scrabble {

void drawIcon(QPainter &painter, Icon icon, const QRectF &rect, const QColor &color) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal side = qMin(rect.width(), rect.height());
    const QPointF origin = rect.topLeft();
    const auto pt = [&](qreal x, qreal y) { return origin + QPointF(x, y) * side; };
    painter.setPen(QPen(QBrush(color), side / 12.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    QPainterPath path;
    QPainterPath solid;
    switch (icon) {
    case Icon::Play:
        solid.moveTo(pt(0.3, 0.2));
        solid.lineTo(pt(0.8, 0.5));
        solid.lineTo(pt(0.3, 0.8));
        solid.closeSubpath();
        break;
    case Icon::Recall:
        path.moveTo(pt(0.2, 0.2));
        path.lineTo(pt(0.2, 0.4));
        path.lineTo(pt(0.42, 0.4));
        path.moveTo(pt(0.2, 0.4));
        path.cubicTo(pt(0.25, 0.1), pt(0.85, 0.1), pt(0.8, 0.62));
        break;
    case Icon::Shuffle:
        path.moveTo(pt(0.15, 0.7));
        path.lineTo(pt(0.85, 0.3));
        path.moveTo(pt(0.65, 0.3));
        path.lineTo(pt(0.85, 0.3));
        path.lineTo(pt(0.85, 0.5));
        path.moveTo(pt(0.15, 0.3));
        path.lineTo(pt(0.85, 0.7));
        path.moveTo(pt(0.65, 0.7));
        path.lineTo(pt(0.85, 0.7));
        path.lineTo(pt(0.85, 0.5));
        break;
    case Icon::Pass:
        path.moveTo(pt(0.18, 0.25));
        path.lineTo(pt(0.48, 0.5));
        path.lineTo(pt(0.18, 0.75));
        path.moveTo(pt(0.5, 0.25));
        path.lineTo(pt(0.8, 0.5));
        path.lineTo(pt(0.5, 0.75));
        break;
    case Icon::Exchange:
        path.moveTo(pt(0.18, 0.36));
        path.lineTo(pt(0.82, 0.36));
        path.moveTo(pt(0.64, 0.2));
        path.lineTo(pt(0.82, 0.36));
        path.lineTo(pt(0.64, 0.52));
        path.moveTo(pt(0.82, 0.66));
        path.lineTo(pt(0.18, 0.66));
        path.moveTo(pt(0.36, 0.5));
        path.lineTo(pt(0.18, 0.66));
        path.lineTo(pt(0.36, 0.82));
        break;
    case Icon::Hint:
        path.addEllipse(pt(0.5, 0.38), side * 0.25, side * 0.25);
        path.moveTo(pt(0.38, 0.72));
        path.lineTo(pt(0.62, 0.72));
        path.moveTo(pt(0.42, 0.85));
        path.lineTo(pt(0.58, 0.85));
        break;
    case Icon::NewGame:
        path.addRoundedRect(QRectF(pt(0.15, 0.15), pt(0.85, 0.85)), side * 0.18, side * 0.18);
        path.moveTo(pt(0.5, 0.32));
        path.lineTo(pt(0.5, 0.68));
        path.moveTo(pt(0.32, 0.5));
        path.lineTo(pt(0.68, 0.5));
        break;
    case Icon::Close:
        path.moveTo(pt(0.27, 0.27));
        path.lineTo(pt(0.73, 0.73));
        path.moveTo(pt(0.73, 0.27));
        path.lineTo(pt(0.27, 0.73));
        break;
    case Icon::Rules:
        path.moveTo(pt(0.3, 0.36));
        path.arcTo(QRectF(pt(0.3, 0.16), pt(0.7, 0.56)), 180, -270);
        path.lineTo(pt(0.5, 0.66));
        solid.addEllipse(pt(0.5, 0.8), side * 0.045, side * 0.045);
        break;
    case Icon::Check:
        path.moveTo(pt(0.2, 0.52));
        path.lineTo(pt(0.42, 0.74));
        path.lineTo(pt(0.8, 0.3));
        break;
    case Icon::Stats:
        path.moveTo(pt(0.25, 0.8));
        path.lineTo(pt(0.25, 0.5));
        path.moveTo(pt(0.5, 0.8));
        path.lineTo(pt(0.5, 0.25));
        path.moveTo(pt(0.75, 0.8));
        path.lineTo(pt(0.75, 0.4));
        break;
    }

    painter.drawPath(path);
    painter.setBrush(QBrush(color));
    painter.drawPath(solid);
    painter.restore();
}

QIcon makeIcon(Icon icon, const QColor &color, int size, qreal devicePixelRatio) {
    const int pixels = qRound(size * devicePixelRatio);
    QPixmap pixmap(pixels, pixels);
    pixmap.setDevicePixelRatio(devicePixelRatio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    drawIcon(painter, icon, QRectF(0, 0, size, size), color);
    painter.end();
    return QIcon(pixmap);
}

}
