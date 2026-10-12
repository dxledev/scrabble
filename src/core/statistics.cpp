/**
 * @file statistics.cpp
 * @brief Lifetime win, loss, and score totals per opponent level.
 */
#include "statistics.h"
#include "storage.h"

#include <QJsonArray>
#include <algorithm>
#include <stdexcept>

namespace scrabble {
namespace {

int readCount(const QJsonObject &data, const char *key) {
    const auto value = data.value(key);
    if (!value.isDouble() || value.toDouble() != value.toInt(-1) || value.toInt(-1) < 0)
        throw std::invalid_argument("Invalid statistics value");
    return value.toInt();
}

}

Statistics Statistics::load(const QString &path) {
    Statistics result;
    try {
        const auto data = readJson(path);
        const auto levels = data.value("levels").toArray();
        if (data.value("version").toInt() != 1 || levels.size() != kLevels)
            return result;
        for (int index = 0; index < kLevels; ++index) {
            const auto entry = levels[index].toObject();
            auto &stats = result.levels_[index];
            stats.games = readCount(entry, "games");
            stats.wins = readCount(entry, "wins");
            stats.losses = readCount(entry, "losses");
            stats.ties = readCount(entry, "ties");
            stats.bestScore = readCount(entry, "best_score");
            stats.bestPlay = readCount(entry, "best_play");
            stats.totalScore = readCount(entry, "total_score");
        }
    } catch (const std::exception &) {
        return Statistics();
    }
    return result;
}

void Statistics::save(const QString &path) const {
    QJsonArray levels;
    for (const auto &stats : levels_) {
        levels.append(QJsonObject{{"games", stats.games}, {"wins", stats.wins}, {"losses", stats.losses},
                                  {"ties", stats.ties}, {"best_score", stats.bestScore},
                                  {"best_play", stats.bestPlay}, {"total_score", int(stats.totalScore)}});
    }
    writeJson(path, {{"version", 1}, {"levels", levels}}, QJsonDocument::Compact);
}

void Statistics::recordGame(int level, int playerScore, int computerScore, int bestPlay) {
    auto &stats = levels_[std::clamp(level, 0, kLevels - 1)];
    ++stats.games;
    if (playerScore > computerScore)
        ++stats.wins;
    else if (playerScore < computerScore)
        ++stats.losses;
    else
        ++stats.ties;
    stats.bestScore = std::max(stats.bestScore, playerScore);
    stats.bestPlay = std::max(stats.bestPlay, bestPlay);
    stats.totalScore += std::max(0, playerScore);
}

}
