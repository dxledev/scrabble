/**
 * @file rules.cpp
 * @brief Standard Scrabble board layout, tile values, and tile distribution.
 */
#include "rules.h"

#include <array>

namespace scrabble {
namespace {

// '.' none, 'l' double letter, 'L' triple letter, 'w' double word, 'W' triple word.
constexpr std::array<const char *, kBoardSize> layout{
    "W..l...W...l..W", ".w...L...L...w.", "..w...l.l...w..", "l..w...l...w..l", "....w.....w....",
    ".L...L...L...L.", "..l...l.l...l..", "W..l...w...l..W", "..l...l.l...l..", ".L...L...L...L.",
    "....w.....w....", "l..w...l...w..l", "..w...l.l...w..", ".w...L...L...w.", "W..l...W...l..W",
};

constexpr std::array<int, 26> values{1, 3, 3, 2, 1, 4, 2, 4, 1, 8, 5, 1, 3, 1, 1, 3, 10, 1, 1, 1, 1, 4, 4, 8, 4, 10};
constexpr std::array<int, 26> counts{9, 2, 2, 4, 12, 2, 3, 2, 9, 1, 1, 4, 2, 6, 8, 2, 1, 6, 4, 6, 4, 2, 2, 1, 2, 1};

}

Premium premiumAt(int row, int col) {
    if (row < 0 || row >= kBoardSize || col < 0 || col >= kBoardSize)
        return Premium::None;
    switch (layout[row][col]) {
    case 'l': return Premium::DoubleLetter;
    case 'L': return Premium::TripleLetter;
    case 'w': return Premium::DoubleWord;
    case 'W': return Premium::TripleWord;
    default: return Premium::None;
    }
}

int letterValue(char tile) {
    return tile >= 'A' && tile <= 'Z' ? values[tile - 'A'] : 0;
}

int tileCount(char tile) {
    if (tile == '?')
        return 2;
    return tile >= 'A' && tile <= 'Z' ? counts[tile - 'A'] : 0;
}

}
