#pragma once

#include "core/theme.h"

#include <QPainter>
#include <QRectF>

namespace scrabble {

/** @brief Visual state of a drawn tile. */
enum class TileState { Normal, Pending, LastPlay, Selected, Ghost };

/**
 * @brief Draw a near-white tile (theme "tile" color) with a thin darker edge and a centered bold
 * letter in "tile_text". A face value subscript sits at the lower right unless @p blank is true
 * (blank tiles show their stand-in letter in a lighter tone and no value) or @p letter is '?'
 * (an unassigned blank: draw an empty tile with only a small ring). Pending tiles get a 2 px
 * "accent" outline, LastPlay a 1 px accent outline plus a small accent corner mark, Selected a
 * 3 px accent outline with the tile lifted by 2 px of shadow offset (solid, no blur), Ghost is
 * drawn at 45% opacity. No gradients anywhere.
 */
void drawTile(QPainter &painter, const QRectF &rect, char letter, bool blank, const Theme &theme,
              TileState state = TileState::Normal);

}
