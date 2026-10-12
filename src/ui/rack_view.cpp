/**
 * @file rack_view.cpp
 * @brief Painted rack row with clickable slots and a face-down mode.
 *
 * Slots are squares sized from the widget height and width (seven slots plus gaps of about 8% of a
 * slot). Tiles already placed this turn leave an empty outline, and hidden racks paint plain backs.
 */
#include "rack_view.h"
#include "tile_painter.h"
#include "core/rules.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <algorithm>

namespace scrabble {
namespace {

QColor mix(const QColor &from, const QColor &to, qreal amount) {
    const auto blend = [amount](int a, int b) { return qRound(a + (b - a) * amount); };
    return QColor(blend(from.red(), to.red()), blend(from.green(), to.green()), blend(from.blue(), to.blue()));
}

}

RackView::RackView(QWidget *parent) : QWidget(parent), theme_(preset("slate")) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void RackView::setTheme(const Theme &theme) {
    theme_ = theme;
    update();
}

void RackView::setTiles(const std::vector<char> &tiles, const std::vector<bool> &used) {
    tiles_ = tiles;
    used_ = used;
    update();
}

void RackView::setSelected(int index) {
    selected_ = index;
    update();
}

void RackView::setFaceDown(bool hidden) {
    hidden_ = hidden;
    update();
}

QRectF RackView::slotRect(int index) const {
    const qreal side = std::min<qreal>(height(), width() / 7.6);
    const qreal gap = side * 0.08;
    const qreal total = kRackSize * side + (kRackSize - 1) * gap;
    const qreal left = (width() - total) / 2;
    return QRectF(left + index * (side + gap), (height() - side) / 2, side, side);
}

void RackView::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    const QColor border = theme_.color(QStringLiteral("border"));
    const QColor back = mix(theme_.color(QStringLiteral("surface")), border, 0.6);
    const int count = static_cast<int>(tiles_.size());
    for (int index = 0; index < count; ++index) {
        const QRectF rect = slotRect(index);
        const bool used = index < static_cast<int>(used_.size()) && used_[index];
        if (used) {
            const qreal radius = rect.width() * 0.12;
            painter.setPen(QPen(theme_.color(QStringLiteral("grid")), 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
        } else if (hidden_) {
            const qreal radius = rect.width() * 0.12;
            painter.setPen(QPen(border, 1));
            painter.setBrush(back);
            painter.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
        } else {
            const char letter = tiles_[index];
            drawTile(painter, rect, letter, letter == '?', theme_,
                     index == selected_ ? TileState::Selected : TileState::Normal);
        }
    }
}

void RackView::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const int count = static_cast<int>(tiles_.size());
    for (int index = 0; index < count; ++index) {
        const bool used = index < static_cast<int>(used_.size()) && used_[index];
        if (!used && slotRect(index).contains(event->position())) {
            emit tileClicked(index);
            return;
        }
    }
}

}
