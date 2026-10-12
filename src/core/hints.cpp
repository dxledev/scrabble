/**
 * @file hints.cpp
 * @brief Hint allowance by level and the words-played counter that earns more.
 */
#include "hints.h"

namespace scrabble {
namespace {

constexpr int kMaxHints = 99;

}

int HintBank::startingHints(Level level) {
    switch (level) {
    case Level::Easy: return 3;
    case Level::Medium: return 2;
    case Level::Hard: return 1;
    }
    return 2;
}

int HintBank::wordsPerHint(Level level) {
    switch (level) {
    case Level::Easy: return 5;
    case Level::Medium: return 6;
    case Level::Hard: return 7;
    }
    return 6;
}

std::optional<HintBank> HintBank::restore(int available, int progress, Level level) {
    if (available < 0 || available > kMaxHints || progress < 0 || progress >= wordsPerHint(level))
        return std::nullopt;
    HintBank bank(level);
    bank.available_ = available;
    bank.progress_ = progress;
    return bank;
}

bool HintBank::spend() {
    if (available_ <= 0)
        return false;
    --available_;
    return true;
}

bool HintBank::recordWord(Level level) {
    if (++progress_ < wordsPerHint(level))
        return false;
    progress_ = 0;
    if (available_ < kMaxHints)
        ++available_;
    return true;
}

}
