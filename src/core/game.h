#pragma once

#include "dawg.h"
#include "rules.h"

#include <QJsonObject>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace scrabble {

/** @brief One board square. A blank tile stores the letter it stands for with blank = true. */
struct Cell {
    char letter = 0;   ///< 'A'-'Z', or 0 when the square is empty.
    bool blank = false;///< Tile is a blank (worth 0 points).
    bool empty() const { return letter == 0; }
};

using Board = std::array<Cell, kBoardSize * kBoardSize>;

inline Cell &cellAt(Board &board, int row, int col) { return board[row * kBoardSize + col]; }
inline const Cell &cellAt(const Board &board, int row, int col) { return board[row * kBoardSize + col]; }

/** @brief A tile put on the board during one turn. */
struct Placement {
    int row = 0;
    int col = 0;
    char letter = 0;    ///< 'A'-'Z'; for a blank, the letter it represents.
    bool blank = false; ///< Uses a blank tile from the rack.
    bool operator==(const Placement &) const = default;
};

/** @brief A validated, scored play. */
struct ScoredMove {
    std::vector<Placement> tiles;
    int score = 0;               ///< Total including crossword scores and the bingo bonus.
    bool horizontal = true;      ///< Direction of the main word (true for single tiles when the row word is longer or equal).
    bool bingo = false;          ///< All seven rack tiles used.
    std::vector<std::string> words; ///< Every word formed, main word first, uppercase.
};

/** @brief Result of checking a set of placements against the rules and a dictionary. */
struct Evaluation {
    bool valid = false;
    std::string error;  ///< Short human-readable reason when invalid, e.g. "Tiles must be in one row or column".
    ScoredMove move;    ///< Filled in when valid.
};

/**
 * @brief Check and score placements on @p board.
 *
 * Rules: at least one tile; every tile on an empty in-bounds square, no duplicates; all tiles in
 * one row or column; the line they span (including existing tiles between them) has no gaps;
 * the first play on an empty board covers the center (7, 7) and has at least two tiles; later
 * plays touch an existing tile; every word of length >= 2 formed (main word plus each cross
 * word through a new tile) is in @p dictionary. Scoring: letter and word premiums count only for
 * newly placed tiles, blanks score 0, each word is scored separately, and using all seven tiles
 * adds kBingoBonus. Exposed as a free function so move generation can reuse it.
 */
Evaluation evaluatePlacement(const Board &board, const std::vector<Placement> &tiles, const Dawg &dictionary);

/** @brief Kind of a logged turn. */
enum class TurnKind : std::uint8_t { Play, Pass, Exchange };

/** @brief One entry of the move history. */
struct LogEntry {
    int player = 0;
    TurnKind kind = TurnKind::Play;
    int score = 0;
    std::string text; ///< Main word for plays, or number of tiles for exchanges; empty for passes.
};

/**
 * @brief Complete two-player game state: board, bag, racks, scores, turn, and history.
 *
 * Player 0 is the human and player 1 the computer; player 0 moves first. Racks hold 'A'-'Z'
 * and '?' for blanks. The class never consults a dictionary; callers pass plays that were
 * produced by evaluatePlacement().
 */
class Game {
public:
    static constexpr int kHuman = 0;
    static constexpr int kComputer = 1;

    /** @brief Start a new game: shuffle the bag with @p seed and deal seven tiles to each player. */
    explicit Game(std::uint32_t seed);

    const Board &board() const { return board_; }
    const std::vector<char> &rack(int player) const { return racks_[player]; }
    int score(int player) const { return scores_[player]; }
    int bagSize() const { return static_cast<int>(bag_.size()); }
    int turn() const { return turn_; }
    bool finished() const { return finished_; }
    /** @brief 0 or 1 for the winner, -1 for a tie; meaningful only when finished(). */
    int winner() const;
    /** @brief Consecutive turns without scoring (passes, exchanges); the game ends at six. */
    int scorelessTurns() const { return scoreless_; }
    const std::vector<LogEntry> &log() const { return log_; }
    /** @brief Tiles of the most recent play (for highlighting); empty after a pass or exchange. */
    const std::vector<Placement> &lastPlay() const { return lastPlay_; }
    /** @brief Remaining tiles not on the board or in @p player's rack (bag plus opponent rack), as counts per 'A'-'Z' and '?' at index 26. */
    std::array<int, 27> unseenCounts(int player) const;

    /**
     * @brief Apply a play for the current player.
     * @return false (state unchanged) if the game is finished or the rack lacks the tiles
     *         (a blank placement consumes '?'). On success: tiles are placed, score added,
     *         the rack is refilled from the bag, the log is appended, and the turn advances.
     *         Emptying the rack with an empty bag ends the game.
     */
    bool play(const ScoredMove &move);
    /** @brief Current player passes. Six consecutive scoreless turns end the game. Returns false when finished. */
    bool pass();
    /**
     * @brief Current player swaps @p tiles (a sub-multiset of the rack) for new ones from the bag.
     * @return false unless the bag holds at least kRackSize tiles and the rack contains the tiles.
     *         Returned tiles go back into the bag and the bag is reshuffled.
     */
    bool exchange(const std::vector<char> &tiles);
    /** @brief Whether the current player may exchange (bag holds at least kRackSize tiles). */
    bool canExchange() const { return !finished_ && static_cast<int>(bag_.size()) >= kRackSize; }

    QJsonObject toJson() const;
    /** @brief Restore from toJson() output with full validation (tile conservation: exactly the 100-tile distribution). */
    static std::optional<Game> fromJson(const QJsonObject &data);

private:
    Game() = default;
    Board board_{};
    std::array<std::vector<char>, 2> racks_;
    std::vector<char> bag_;
    std::array<int, 2> scores_{};
    int turn_ = 0;
    int scoreless_ = 0;
    bool finished_ = false;
    std::uint32_t rngState_ = 0; ///< Deterministic generator state used by shuffles so saved games stay reproducible.
    std::vector<LogEntry> log_;
    std::vector<Placement> lastPlay_;
    void refill(int player);
    void finishGame(int emptiedPlayer); ///< -1 when ended by scoreless turns.
};

}
