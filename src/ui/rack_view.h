#pragma once

#include "core/theme.h"

#include <QWidget>
#include <vector>

namespace scrabble {

/**
 * @brief Custom-painted row of up to seven rack tiles drawn with drawTile().
 *
 * Slots whose tile is already on the board this turn are drawn as empty outlined slots
 * (border color, no fill). Tiles keep their rack order. The widget is a single painted surface.
 */
class RackView : public QWidget {
    Q_OBJECT
public:
    explicit RackView(QWidget *parent = nullptr);
    void setTheme(const Theme &theme);
    /** @brief Show @p tiles ('A'-'Z' or '?'); used[i] marks tiles already placed on the board. */
    void setTiles(const std::vector<char> &tiles, const std::vector<bool> &used);
    /** @brief Mark one slot selected (TileState::Selected); -1 clears. */
    void setSelected(int index);
    /** @brief Hide the tile faces (shown while the computer is thinking is NOT needed; used for paused state). */
    void setFaceDown(bool hidden);

    QSize sizeHint() const override { return {420, 64}; }
    QSize minimumSizeHint() const override { return {260, 40}; }

signals:
    void tileClicked(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    Theme theme_;
    std::vector<char> tiles_;
    std::vector<bool> used_;
    int selected_ = -1;
    bool hidden_ = false;
    QRectF slotRect(int index) const;
};

}
