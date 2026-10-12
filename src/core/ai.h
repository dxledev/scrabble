#pragma once

#include "game.h"

#include <optional>
#include <random>
#include <vector>

namespace scrabble {

enum class Level { Easy, Medium, Hard };

/** @brief Parse "easy", "medium", or "hard"; unknown strings map to Medium. */
Level parseLevel(const std::string &name);
/** @brief Lowercase name of a level. */
const char *levelName(Level level);

/**
 * @brief Enumerate every legal play for @p rack on @p board.
 *
 * Each returned move is fully validated and scored exactly as evaluatePlacement() would score it
 * (the implementation may call evaluatePlacement as the final check). A '?' in the rack may
 * stand for any letter (blank = true in the placement). Moves are unique by their tile set.
 * On an empty board only plays through the center square are produced. Must complete in well
 * under one second for a typical mid-game board with a full rack.
 */
std::vector<ScoredMove> generateMoves(const Board &board, const std::vector<char> &rack, const Dawg &dictionary);

/**
 * @brief Pick the computer's play, or nullopt when no play exists.
 *
 * Hard: highest score (ties broken randomly). Medium: random among plays scoring at least 60% of
 * the best. Easy: random among plays from the lower half of the score range, preferring words
 * of at most five letters. Uses @p random for all randomness.
 */
std::optional<ScoredMove> chooseMove(const Board &board, const std::vector<char> &rack, const Dawg &dictionary,
                                     Level level, std::mt19937 &random);

/** @brief Tiles to swap when no play exists: everything except the best-keeping letters (keeps E, R, S, T, A, blank when possible). Never empty. */
std::vector<char> chooseExchange(const std::vector<char> &rack, Level level);

}
