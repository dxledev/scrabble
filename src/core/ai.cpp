/**
 * @file ai.cpp
 * @brief Computer opponent: legal move generation over the word graph and level-based move choice.
 */
#include "ai.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace scrabble {
namespace {

constexpr int kSize = kBoardSize;
constexpr int kCenter = kBoardSize / 2;
constexpr int kAlphabet = 26;
constexpr std::uint32_t kAllLetters = (1u << kAlphabet) - 1;
constexpr int kMainWordShort = 5;

using Grid = std::array<std::array<char, kSize>, kSize>;
using CrossRow = std::array<std::uint32_t, kSize>;
using AnchorRow = std::array<bool, kSize>;

// Board letters in one orientation; transposed grids let one scan handle both directions.
struct Frame {
    Grid grid{};
    std::array<CrossRow, kSize> cross{};
    std::array<AnchorRow, kSize> anchors{};
};

// Letters that may stand at (line, pos) given the perpendicular word through that square.
std::uint32_t crossMask(const Grid &grid, int line, int pos, const Dawg &dictionary) {
    std::string above;
    std::string below;
    for (int r = line - 1; r >= 0 && grid[r][pos] != 0; --r)
        above.insert(above.begin(), grid[r][pos]);
    for (int r = line + 1; r < kSize && grid[r][pos] != 0; ++r)
        below.push_back(grid[r][pos]);
    if (above.empty() && below.empty())
        return kAllLetters;
    std::uint32_t mask = 0;
    for (int letter = 0; letter < kAlphabet; ++letter) {
        if (dictionary.contains(above + static_cast<char>('A' + letter) + below))
            mask |= 1u << letter;
    }
    return mask;
}

bool isAnchor(const Grid &grid, int line, int pos, bool boardEmpty) {
    if (grid[line][pos] != 0)
        return false;
    if (boardEmpty)
        return line == kCenter && pos == kCenter;
    constexpr int kSteps[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (const auto &step : kSteps) {
        const int r = line + step[0];
        const int c = pos + step[1];
        if (r >= 0 && r < kSize && c >= 0 && c < kSize && grid[r][c] != 0)
            return true;
    }
    return false;
}

Frame makeFrame(const Board &board, bool transposed, bool boardEmpty, const Dawg &dictionary) {
    Frame frame;
    for (int r = 0; r < kSize; ++r) {
        for (int c = 0; c < kSize; ++c) {
            const char letter = cellAt(board, r, c).letter;
            if (transposed)
                frame.grid[c][r] = letter;
            else
                frame.grid[r][c] = letter;
        }
    }
    for (int line = 0; line < kSize; ++line) {
        for (int pos = 0; pos < kSize; ++pos) {
            frame.anchors[line][pos] = isAnchor(frame.grid, line, pos, boardEmpty);
            if (frame.grid[line][pos] == 0)
                frame.cross[line][pos] = crossMask(frame.grid, line, pos, dictionary);
        }
    }
    return frame;
}

// Appel-Jacobson style search over one orientation: words are built left to right along a line.
class Walk {
public:
    Walk(const Frame &frame, const Dawg &dictionary, std::array<int, kAlphabet> counts, int blanks,
         std::vector<std::vector<Placement>> &plays)
        : frame_(frame), dict_(dictionary), counts_(counts), blanks_(blanks), plays_(plays) {
        available_ = blanks_;
        for (int count : counts_)
            available_ += count;
    }

    /** @brief Record every word on @p line that covers one of its anchors, from the leftmost start. */
    void sweep(int line) {
        line_ = line;
        for (int anchor = 0; anchor < kSize; ++anchor) {
            if (!frame_.anchors[line][anchor])
                continue;
            anchor_ = anchor;
            int leftEmpty = 0;
            for (int start = anchor; start >= 0; --start) {
                if (start < anchor) {
                    if (frame_.anchors[line][start])
                        break;
                    if (frame_.grid[line][start] == 0 && ++leftEmpty > available_ - 1)
                        break;
                }
                if (start == 0 || frame_.grid[line][start - 1] == 0) {
                    start_ = start;
                    extend(start, dict_.root());
                }
            }
        }
    }

private:
    void extend(int pos, std::uint32_t node) {
        if (pos >= kSize || node == 0)
            return;
        const char existing = frame_.grid[line_][pos];
        if (existing != 0) {
            Dawg::Edge edge;
            if (!dict_.find(node, existing, edge))
                return;
            afterLetter(pos, edge);
            extend(pos + 1, edge.child);
            return;
        }
        if (available_ == 0)
            return;
        const std::uint32_t allowed = frame_.cross[line_][pos];
        for (std::uint32_t index = node;; ++index) {
            const Dawg::Edge edge = dict_.edge(index);
            const int letter = edge.letter - 'A';
            if (allowed & (1u << letter)) {
                if (counts_[letter] > 0)
                    take(pos, edge, letter, false);
                if (blanks_ > 0)
                    take(pos, edge, letter, true);
            }
            if (edge.last)
                break;
        }
    }

    void take(int pos, const Dawg::Edge &edge, int letter, bool blank) {
        int &slot = blank ? blanks_ : counts_[letter];
        --slot;
        --available_;
        placed_.push_back(Placement{line_, pos, static_cast<char>('A' + letter), blank});
        afterLetter(pos, edge);
        extend(pos + 1, edge.child);
        placed_.pop_back();
        ++available_;
        ++slot;
    }

    void afterLetter(int pos, const Dawg::Edge &edge) {
        if (!edge.terminal || pos < anchor_ || pos == start_)
            return;
        if (pos + 1 < kSize && frame_.grid[line_][pos + 1] != 0)
            return;
        plays_.push_back(placed_);
    }

    const Frame &frame_;
    const Dawg &dict_;
    std::array<int, kAlphabet> counts_;
    int blanks_;
    int available_ = 0;
    std::vector<std::vector<Placement>> &plays_;
    std::vector<Placement> placed_;
    int line_ = 0;
    int start_ = 0;
    int anchor_ = 0;
};

// Identity of a play for deduplication: placements in evaluatePlacement's sorted order, blanks included.
std::string keyOf(const std::vector<Placement> &tiles) {
    std::string key;
    for (const Placement &tile : tiles) {
        key.push_back(static_cast<char>(tile.row));
        key.push_back(static_cast<char>(tile.col));
        key.push_back(tile.letter);
        key.push_back(tile.blank ? '1' : '0');
    }
    return key;
}

std::string mainWord(const ScoredMove &move) {
    return move.words.empty() ? std::string() : move.words.front();
}

}  // namespace

Level parseLevel(const std::string &name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (lower == "easy")
        return Level::Easy;
    if (lower == "hard")
        return Level::Hard;
    return Level::Medium;
}

const char *levelName(Level level) {
    switch (level) {
    case Level::Easy: return "easy";
    case Level::Medium: return "medium";
    case Level::Hard: return "hard";
    }
    return "medium";
}

std::vector<ScoredMove> generateMoves(const Board &board, const std::vector<char> &rack, const Dawg &dictionary) {
    std::array<int, kAlphabet> counts{};
    int blanks = 0;
    for (char tile : rack) {
        if (tile == '?')
            ++blanks;
        else if (tile >= 'A' && tile <= 'Z')
            ++counts[tile - 'A'];
    }

    std::vector<ScoredMove> moves;
    if (blanks == 0 && std::all_of(counts.begin(), counts.end(), [](int n) { return n == 0; }))
        return moves;

    const bool boardEmpty = std::all_of(board.begin(), board.end(), [](const Cell &cell) { return cell.empty(); });
    std::set<std::string> seen;
    for (bool transposed : {false, true}) {
        const Frame frame = makeFrame(board, transposed, boardEmpty, dictionary);
        std::vector<std::vector<Placement>> plays;
        Walk walk(frame, dictionary, counts, blanks, plays);
        for (int line = 0; line < kSize; ++line)
            walk.sweep(line);

        for (std::vector<Placement> &play : plays) {
            if (transposed) {
                for (Placement &tile : play)
                    std::swap(tile.row, tile.col);
            }
            Evaluation evaluation = evaluatePlacement(board, play, dictionary);
            if (evaluation.valid && seen.insert(keyOf(evaluation.move.tiles)).second)
                moves.push_back(std::move(evaluation.move));
        }
    }
    return moves;
}

std::optional<ScoredMove> chooseMove(const Board &board, const std::vector<char> &rack, const Dawg &dictionary,
                                     Level level, std::mt19937 &random) {
    std::vector<ScoredMove> moves = generateMoves(board, rack, dictionary);
    if (moves.empty())
        return std::nullopt;

    std::stable_sort(moves.begin(), moves.end(), [](const ScoredMove &a, const ScoredMove &b) {
        if (a.score != b.score)
            return a.score > b.score;
        if (a.tiles.size() != b.tiles.size())
            return a.tiles.size() < b.tiles.size();
        return mainWord(a) < mainWord(b);
    });

    const int best = moves.front().score;
    std::vector<const ScoredMove *> pool;
    switch (level) {
    case Level::Hard:
        for (const ScoredMove &move : moves) {
            if (move.score == best)
                pool.push_back(&move);
        }
        break;
    case Level::Medium:
        for (const ScoredMove &move : moves) {
            if (move.score * 5 >= best * 3)
                pool.push_back(&move);
        }
        break;
    case Level::Easy: {
        const std::size_t first = moves.size() >= 2 ? moves.size() / 2 : 0;
        std::vector<const ScoredMove *> shortWords;
        for (std::size_t i = first; i < moves.size(); ++i) {
            pool.push_back(&moves[i]);
            if (mainWord(moves[i]).size() <= kMainWordShort)
                shortWords.push_back(&moves[i]);
        }
        if (!shortWords.empty())
            pool = std::move(shortWords);
        break;
    }
    }
    return *pool[random() % pool.size()];
}

std::vector<char> chooseExchange(const std::vector<char> &rack, Level level) {
    constexpr char kKeepOrder[] = "?SERTAINOLDGHUYCMPBFK";
    constexpr int kMaxCopies = 2;
    if (rack.size() <= 1)
        return rack;
    const std::size_t cap = std::min<std::size_t>(level == Level::Easy ? 2 : 4, rack.size() - 1);

    std::vector<char> rest = rack;
    std::size_t kept = 0;
    for (char wanted : kKeepOrder) {
        for (int copies = 0; copies < kMaxCopies && kept < cap; ++copies) {
            const auto it = std::find(rest.begin(), rest.end(), wanted);
            if (it == rest.end())
                break;
            rest.erase(it);
            ++kept;
        }
    }
    return rest;
}

}  // namespace scrabble
