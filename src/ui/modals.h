#pragma once

#include "core/statistics.h"
#include "core/theme.h"
#include "modal_backdrop.h"

#include <QAbstractButton>
#include <vector>

class QVBoxLayout;

namespace scrabble {

/** @brief Checkable button that paints a tile face with the theme's tile colors. */
class TileButton : public QAbstractButton {
    Q_OBJECT
public:
    TileButton(char letter, const Theme &theme, QWidget *parent = nullptr);
    void setTheme(const Theme &theme);
    char letter() const { return letter_; }
    QSize sizeHint() const override { return {44, 44}; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    char letter_;
    Theme theme_;
};

/**
 * @brief Every popup of the game, drawn inside the window over a blurred snapshot of it.
 *
 * One reusable layer rebuilds its card for each request, so no separate native surface is ever
 * created. Escape or a click outside the card dismisses it without choosing anything.
 */
class Modal : public ModalBackdrop {
    Q_OBJECT
public:
    explicit Modal(QWidget *parent, QWidget *background);
    void setTheme(const Theme &theme);

    /** @brief Title, text, and a row of buttons; emits answered(index) for the chosen button. */
    void showMessage(const QString &title, const QString &body, const QStringList &buttons, int primaryIndex = 0);
    /** @brief Choose the letter a blank tile stands for; emits letterChosen. */
    void showLetterPicker();
    /** @brief Choose rack tiles to swap; emits exchangeChosen with the selected letters. */
    void showExchange(const std::vector<char> &rack);
    /** @brief Choose an opponent level and start; emits newGameChosen(0..2). */
    void showNewGame(int currentLevel, bool inProgress);
    /** @brief Lifetime results for all three levels. */
    void showStats(const Statistics &statistics);
    /** @brief Short rules and controls. */
    void showRules();

signals:
    void answered(int index);
    void letterChosen(char letter);
    void exchangeChosen(const std::vector<char> &tiles);
    void newGameChosen(int level);
    /** @brief Closed by Escape, an outside click, or a Cancel/Close button. */
    void dismissed();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool focusNextPrevChild(bool next) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    Theme theme_;
    bool lettersOnly_ = false;
    QAbstractButton *defaultButton_ = nullptr;
    QVBoxLayout *cardLayout_ = nullptr;
    /** @brief Replace the card with an empty one titled @p title and return its layout. */
    QVBoxLayout *beginCard(const QString &title, int width);
    /** @brief Add a row of buttons; the one at @p primaryIndex is emphasized. */
    QList<QAbstractButton *> addButtons(const QStringList &labels, int primaryIndex);
    void display(QAbstractButton *focus);
    void dismiss();
    void fitCard();
    int cardWidth_ = 460;
    int width_ = 0; ///< Layer width used to size the card, kept so fitCard() works before the first show.
};

}
