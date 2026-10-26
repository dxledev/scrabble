/**
 * @file modal_motion.cpp
 * @brief Card fade and scale used when a modal opens or closes.
 */
#include "modal_motion.h"

#include <QPainter>
#include <QScreen>
#include <QWidget>
#include <algorithm>

namespace scrabble {

ModalMotion::ModalMotion(QObject *parent) : QGraphicsEffect(parent) {
    setObjectName("modalMotion");
    timeline_.setParent(this);
    timeline_.setObjectName("modalTimeline");
    timeline_.setDuration(duration);
    timeline_.setEasingCurve(QEasingCurve::OutCubic);
    connect(&timeline_, &QTimeLine::valueChanged, this, [this](qreal value) {
        setProgress(startProgress_ + ((targetOpen_ ? 1.0 : 0.0) - startProgress_) * value);
    });
    connect(&timeline_, &QTimeLine::finished, this, [this] { emit finished(targetOpen_); });
}

void ModalMotion::setProgress(qreal value) {
    progress_ = value;
    update();
    emit progressChanged(value);
}

void ModalMotion::setInitial(qreal progress) {
    timeline_.stop();
    progress_ = progress;
    update();
}

void ModalMotion::stop() {
    timeline_.stop();
}

void ModalMotion::animate(bool open) {
    timeline_.stop();
    targetOpen_ = open;
    const qreal target = open ? 1 : 0;
    if (qEnvironmentVariableIntValue("SCRABBLE_REDUCED_MOTION") == 1 || progress_ == target) {
        setProgress(target);
        emit finished(open);
        return;
    }
    startProgress_ = progress_;
    // Opening decelerates into place; closing is the mirror, accelerating away, so it does not
    // drop most of the way in the first frame and read as an instant cut.
    timeline_.setEasingCurve(open ? QEasingCurve::OutCubic : QEasingCurve::InCubic);
    const auto *widget = qobject_cast<QWidget *>(parent());
    const auto *screen = widget ? widget->screen() : nullptr;
    const double refresh = screen && screen->refreshRate() > 0 ? screen->refreshRate() : 60;
    timeline_.setUpdateInterval(std::max(1, qRound(1000 / refresh)));
    timeline_.start();
}

void ModalMotion::draw(QPainter *painter) {
    if (progress_ <= 0)
        return;
    if (progress_ >= 1) {
        drawSource(painter);
        return;
    }
    QPoint offset;
    const auto pixmap = sourcePixmap(Qt::LogicalCoordinates, &offset, NoPad);
    const auto center = sourceBoundingRect(Qt::LogicalCoordinates).center();
    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->setOpacity(painter->opacity() * progress_);
    painter->translate(center);
    painter->scale(scale(), scale());
    painter->translate(-center);
    painter->drawPixmap(offset, pixmap);
    painter->restore();
}

}
