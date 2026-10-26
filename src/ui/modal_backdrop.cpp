/**
 * @file modal_backdrop.cpp
 * @brief Blurred, tinted background layer shared by embedded modal cards.
 *
 * A snapshot is captured when the overlay appears and blurred for painting.
 * The card remains an interactive child; presses elsewhere emit a signal for
 * the owning modal to dismiss itself.
 */
#include "modal_backdrop.h"

#include <QGraphicsBlurEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QPainter>
#include <QMouseEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QResizeEvent>
#include <QLayout>
#include <algorithm>

namespace scrabble {
namespace {

QPixmap extendEdges(const QPixmap &snapshot, int padding) {
    const auto source = snapshot.toImage().convertToFormat(QImage::Format_RGB32);
    QImage extended(source.size() + QSize(padding * 2, padding * 2), QImage::Format_RGB32);
    for (int y = 0; y < extended.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(source.constScanLine(std::clamp(y - padding, 0, source.height() - 1)));
        auto *destination = reinterpret_cast<QRgb *>(extended.scanLine(y));
        std::fill_n(destination, padding, row[0]);
        std::copy_n(row, source.width(), destination + padding);
        std::fill_n(destination + padding + source.width(), padding, row[source.width() - 1]);
    }
    return QPixmap::fromImage(extended);
}

}

ModalBackdrop::ModalBackdrop(QWidget *parent, QWidget *background) : QWidget(parent), background_(background) {
    setObjectName("modalBackdrop");
}

void ModalBackdrop::capture(const QPixmap &snapshot) {
    // Blur a reduced snapshot once; the idle modal retains only this small image.
    const auto logicalSize = snapshot.deviceIndependentSize().toSize();
    auto reducedSize = (logicalSize / 3).expandedTo(QSize(1, 1));
    if (reducedSize.width() > 640 || reducedSize.height() > 640)
        reducedSize.scale(640, 640, Qt::KeepAspectRatio);
    auto reduced = snapshot.scaled(reducedSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    reduced.setDevicePixelRatio(1);
    // Repeated edge pixels keep the blur opaque and prevent halos at window boundaries.
    constexpr int padding = 32;
    QGraphicsScene scene;
    auto *image = scene.addPixmap(extendEdges(reduced, padding));
    auto *blur = new QGraphicsBlurEffect;
    blur->setBlurRadius(5);
    blur->setBlurHints(QGraphicsBlurEffect::QualityHint);
    image->setGraphicsEffect(blur);
    const QRectF bounds(QPointF(), reduced.size());
    const QRectF sourceBounds(QPointF(padding, padding), reduced.size());
    QPixmap blurred(reduced.size());
    blurred.fill(Qt::transparent);
    QPainter painter(&blurred);
    scene.render(&painter, bounds, sourceBounds);
    painter.end();
    snapshot_ = std::move(blurred);
    update();
}

void ModalBackdrop::clearSnapshot() {
    snapshot_ = QPixmap();
}

void ModalBackdrop::setTint(const QColor &color) {
    tint_ = color;
    tint_.setAlpha(105);
    if (isVisible())
        captureBackground();
    update();
}

void ModalBackdrop::setProgress(qreal progress) {
    if (progress_ == progress)
        return;
    progress_ = progress;
    update();
    emit progressChanged(progress);
}

void ModalBackdrop::paintEvent(QPaintEvent *) {
    // At zero nothing is drawn, so the live window shows through unchanged: no flash on entry or exit.
    if (progress_ <= 0 || snapshot_.isNull())
        return;
    QPainter painter(this);
    painter.setOpacity(progress_);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawPixmap(rect(), snapshot_);
    painter.fillRect(rect(), tint_);
}

void ModalBackdrop::captureBackground() {
    background_->layout()->activate();
    QPixmap snapshot(background_->size() * background_->devicePixelRatioF());
    snapshot.setDevicePixelRatio(background_->devicePixelRatioF());
    snapshot.fill(QColor::fromRgb(tint_.rgb()));
    QPainter painter(&snapshot);
    background_->render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
    painter.end();
    capture(snapshot);
}

void ModalBackdrop::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    if (snapshot_.isNull())
        captureBackground();
}

void ModalBackdrop::hideEvent(QHideEvent *event) {
    QWidget::hideEvent(event);
    clearSnapshot();
}

void ModalBackdrop::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (isVisible())
        captureBackground();
}

void ModalBackdrop::mousePressEvent(QMouseEvent *event) {
    if (card_ && !card_->geometry().contains(event->position().toPoint())) {
        emit outsideClicked();
        event->accept();
    } else {
        QWidget::mousePressEvent(event);
    }
}

}
