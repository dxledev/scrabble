#pragma once

#include <QGraphicsEffect>
#include <QTimeLine>

namespace scrabble {

/**
 * @brief Fade-and-settle animation applied to a modal card as a graphics effect.
 *
 * Progress runs 0 (hidden) to 1 (shown). The card fades in while scaling from 98% to 100%;
 * the owning modal feeds the same progress to its backdrop so both move together.
 */
class ModalMotion : public QGraphicsEffect {
    Q_OBJECT
public:
    static constexpr int duration = 120;
    explicit ModalMotion(QObject *parent = nullptr);
    qreal progress() const { return progress_; }
    qreal scale() const { return .98 + .02 * progress_; }
    /** @brief Animate toward fully shown (true) or fully hidden (false) from the current progress. */
    void animate(bool open);
    /** @brief Halt any running animation without changing progress. */
    void stop();
    /** @brief Jump to @p progress without animating or notifying. */
    void setInitial(qreal progress);

signals:
    void progressChanged(qreal progress);
    /** @brief Emitted once the animation reaches its target, immediately if there is nothing to animate. */
    void finished(bool open);

protected:
    void draw(QPainter *painter) override;

private:
    QTimeLine timeline_;
    qreal startProgress_ = 0;
    qreal progress_ = 0;
    bool targetOpen_ = false;
    void setProgress(qreal value);
};

}
