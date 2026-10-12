#pragma once

#include "ai.h"

#include <optional>

namespace scrabble {

/**
 * @brief Hints the player may spend, and the progress toward earning another.
 *
 * A game starts with 3 hints on Easy, 2 on Medium, and 1 on Hard. Every 5 (Easy), 6 (Medium), or
 * 7 (Hard) words played earns one more.
 */
class HintBank {
public:
    /** @brief Hints a new game starts with at @p level. */
    static int startingHints(Level level);
    /** @brief Words that must be played to earn one hint at @p level. */
    static int wordsPerHint(Level level);

    /** @brief A fresh bank for a new game at @p level. */
    explicit HintBank(Level level) : available_(startingHints(level)) {}
    /** @brief Rebuild a saved bank; nullopt when the values are out of range for @p level. */
    static std::optional<HintBank> restore(int available, int progress, Level level);

    int available() const { return available_; }
    /** @brief Words played since the last hint was earned. */
    int progress() const { return progress_; }
    /** @brief Words still needed for the next hint at @p level. */
    int wordsUntilNext(Level level) const { return wordsPerHint(level) - progress_; }

    /** @brief Spend one hint; false when none is left. */
    bool spend();
    /** @brief Count one played word; true when it earned a hint. */
    bool recordWord(Level level);

private:
    HintBank() = default;
    int available_ = 0;
    int progress_ = 0;
};

}
