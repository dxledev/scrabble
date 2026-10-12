#pragma once

#include <cstdint>

namespace scrabble {

constexpr int kBoardSize = 15;
constexpr int kRackSize = 7;
constexpr int kBingoBonus = 50;
constexpr int kTileTotal = 100;

/** @brief Square bonus kinds of the standard 15 x 15 board. */
enum class Premium : std::uint8_t { None, DoubleLetter, TripleLetter, DoubleWord, TripleWord };

/** @brief Standard premium layout. The center square (7, 7) is a double word. */
Premium premiumAt(int row, int col);
/** @brief Face value of a tile: 'A'-'Z' use standard values and '?' (blank) is worth 0. */
int letterValue(char tile);
/** @brief Number of copies of @p tile ('A'-'Z' or '?') in a full bag; 100 in total. */
int tileCount(char tile);

}
