#pragma once

#include <QString>
#include <array>

namespace scrabble {

/** @brief Lifetime totals for one opponent level. */
struct LevelStats {
    int games = 0;          ///< Completed games.
    int wins = 0;
    int losses = 0;
    int ties = 0;
    int bestScore = 0;      ///< Highest final score the player reached.
    int bestPlay = 0;       ///< Highest single play the player made.
    long long totalScore = 0; ///< Sum of final player scores, for the average.
};

/** @brief Local statistics for all three levels, saved as stats.json beside the game. */
class Statistics {
public:
    static constexpr int kLevels = 3; ///< Easy, Medium, Hard.

    /** @brief Load from @p path; a missing or invalid file yields empty statistics. */
    static Statistics load(const QString &path);
    /** @brief Atomically write to @p path. */
    void save(const QString &path) const;
    /** @brief Record a finished game at @p level (0 easy, 1 medium, 2 hard). */
    void recordGame(int level, int playerScore, int computerScore, int bestPlay);
    const LevelStats &level(int index) const { return levels_[index]; }

private:
    std::array<LevelStats, kLevels> levels_{};
};

}
