#pragma once

#include <QPixmap>
#include <QWidget>

namespace scrabble {

/** @brief In-window modal surface with a blurred snapshot and outside-click signal. */
class ModalBackdrop : public QWidget {
    Q_OBJECT
public:
    /** @brief Create a modal layer over @p background inside @p parent. */
    explicit ModalBackdrop(QWidget *parent, QWidget *background);
    /** @brief Set the translucent tint drawn above the captured background. */
    void setTint(const QColor &color);
    /** @brief Fade the blurred layer between hidden (0) and fully shown (1). */
    void setProgress(qreal progress);
    qreal progress() const { return progress_; }

signals:
    /** @brief Emitted whenever the fade progress changes. */
    void progressChanged(qreal progress);
    /** @brief Emitted when a click lands outside the modal card. */
    void outsideClicked();

protected:
    QWidget *card_ = nullptr;
    /** @brief Capture and blur the background now so showing it needs no further work. */
    void prepare() { captureBackground(); }
    /** @brief Draw the blurred snapshot and tint behind the modal card. */
    void paintEvent(QPaintEvent *event) override;
    /** @brief Capture the background and size the card when shown. */
    void showEvent(QShowEvent *event) override;
    /** @brief Release the captured background when hidden. */
    void hideEvent(QHideEvent *event) override;
    /** @brief Keep the card centered after a parent resize. */
    void resizeEvent(QResizeEvent *event) override;
    /** @brief Emit outsideClicked when the press does not land on the card. */
    void mousePressEvent(QMouseEvent *event) override;

private:
    QWidget *background_;
    QPixmap snapshot_;
    QColor tint_;
    qreal progress_ = 1;
    /** @brief Store and blur a newly captured background image. */
    void capture(const QPixmap &snapshot);
    /** @brief Release the image to avoid retaining a hidden modal snapshot. */
    void clearSnapshot();
    /** @brief Capture the background widget at the modal's display resolution. */
    void captureBackground();
};

}
