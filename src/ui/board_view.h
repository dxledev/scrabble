#pragma once

#include "core/game.h"
#include "core/theme.h"

#include <QWidget>
#include <vector>

namespace scrabble {

/**
 * @brief Custom-painted 15 x 15 board: one widget, no per-cell child widgets.
 *
 * The board is drawn as the largest centered square that fits, with premium squares tinted by
 * the theme (premium color mixed ~55% into "surface_alt", with a short label such as 2L, 3L,
 * 2W, 3W and a star on the center), solid grid lines, and tiles drawn with drawTile().
 * Colors come only from the Theme. Nothing is painted with gradients.
 */
class BoardView : public QWidget {
    Q_OBJECT
public:
    explicit BoardView(QWidget *parent = nullptr);
    void setTheme(const Theme &theme);
    /** @brief Committed tiles. */
    void setBoard(const Board &board);
    /** @brief Tiles placed this turn and not yet played (drawn with TileState::Pending). */
    void setPending(const std::vector<Placement> &pending);
    /** @brief Tiles of the previous play (TileState::LastPlay). */
    void setLastPlay(const std::vector<Placement> &tiles);
    /** @brief Keyboard cursor outline on (row, col) with a small arrow showing the typing direction; row < 0 hides it. */
    void setKeyboardCursor(int row, int col, bool horizontal);
    /** @brief Rectangle of the board in widget coordinates. */
    QRectF boardRect() const;

    QSize sizeHint() const override { return {640, 640}; }
    QSize minimumSizeHint() const override { return {330, 330}; }

signals:
    /** @brief Left click on an empty square, or a committed tile (Window decides what to do). */
    void squareClicked(int row, int col);
    /** @brief Click on a pending tile (Window recalls it). */
    void pendingClicked(int row, int col);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    Theme theme_;
    Board board_{};
    std::vector<Placement> pending_;
    std::vector<Placement> lastPlay_;
    int cursorRow_ = -1;
    int cursorCol_ = -1;
    bool cursorHorizontal_ = true;
    bool squareAt(const QPointF &position, int &row, int &col) const;
};

}
