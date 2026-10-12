/**
 * @file ai_tests.cpp
 * @brief Move generation completeness and validity, blanks, timing, and level-based move choice.
 */
#include "core/ai.h"
#include "core/dawg.h"
#include "core/game.h"
#include "core/rules.h"

#include <QElapsedTimer>
#include <QTest>
#include <algorithm>
#include <array>
#include <cstddef>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace scrabble;

namespace {

constexpr int kCenterRow = kBoardSize / 2;
constexpr int kCenterCol = kBoardSize / 2;

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

bool inBoard(int row, int col) {
    return row >= 0 && row < kBoardSize && col >= 0 && col < kBoardSize;
}

/** Places @p word from (row, col) along (dr, dc), skipping squares that already hold the same letter. */
void playWord(Board &board, const std::string &word, int row, int col, int dr, int dc) {
    std::vector<Placement> tiles;
    for (std::size_t i = 0; i < word.size(); ++i) {
        const int r = row + static_cast<int>(i) * dr;
        const int c = col + static_cast<int>(i) * dc;
        const Cell &cell = cellAt(board, r, c);
        if (cell.empty())
            tiles.push_back(Placement{r, c, word[i], false});
        else
            QCOMPARE(cell.letter, word[i]);
    }
    const Evaluation evaluation = evaluatePlacement(board, tiles, Dawg::english());
    QVERIFY2(evaluation.valid, evaluation.error.c_str());
    for (const Placement &tile : tiles)
        cellAt(board, tile.row, tile.col) = Cell{tile.letter, tile.blank};
}

/** Hand-built mid-game board: ZEBRA across the center, HOUSE down onto its E, AXE down from its A. */
Board midGameBoard() {
    Board board{};
    playWord(board, "ZEBRA", kCenterRow, kCenterCol - 2, 0, 1);
    playWord(board, "HOUSE", 3, kCenterCol - 1, 1, 0);
    playWord(board, "AXE", kCenterRow, kCenterCol + 2, 1, 0);
    return board;
}

/**
 * Exhaustive oracle: every legal play whose first new tile sits on one of @p starts and whose
 * later tiles follow in direction (dr, dc), skipping occupied squares. Each candidate is
 * checked by evaluatePlacement, so the result is exactly the set of legal plays reachable from
 * those starts with the given directions.
 */
void bruteForceFrom(const Board &board, const std::vector<char> &rack, int r, int c, int dr, int dc,
                    std::vector<bool> &used, std::vector<Placement> &tiles, std::vector<ScoredMove> &out) {
    if (!inBoard(r, c))
        return;
    if (!cellAt(board, r, c).empty()) {
        bruteForceFrom(board, rack, r + dr, c + dc, dr, dc, used, tiles, out);
        return;
    }
    for (std::size_t i = 0; i < rack.size(); ++i) {
        if (used[i])
            continue;
        used[i] = true;
        tiles.push_back(Placement{r, c, rack[i], false});
        const Evaluation evaluation = evaluatePlacement(board, tiles, Dawg::english());
        if (evaluation.valid)
            out.push_back(evaluation.move);
        if (tiles.size() < rack.size())
            bruteForceFrom(board, rack, r + dr, c + dc, dr, dc, used, tiles, out);
        tiles.pop_back();
        used[i] = false;
    }
}

std::vector<ScoredMove> bruteForce(const Board &board, const std::vector<char> &rack,
                                   const std::vector<std::pair<int, int>> &starts,
                                   const std::vector<std::pair<int, int>> &directions) {
    std::vector<ScoredMove> out;
    for (const auto &[row, col] : starts) {
        if (!cellAt(board, row, col).empty())
            continue;
        for (const auto &[dr, dc] : directions) {
            std::vector<bool> used(rack.size(), false);
            std::vector<Placement> tiles;
            bruteForceFrom(board, rack, row, col, dr, dc, used, tiles, out);
        }
    }
    return out;
}

std::set<std::string> keysOf(const std::vector<ScoredMove> &moves) {
    std::set<std::string> keys;
    for (const ScoredMove &move : moves)
        keys.insert(keyOf(move.tiles));
    return keys;
}

std::vector<std::pair<int, int>> allSquares() {
    std::vector<std::pair<int, int>> squares;
    for (int r = 0; r < kBoardSize; ++r)
        for (int c = 0; c < kBoardSize; ++c)
            squares.emplace_back(r, c);
    return squares;
}

std::vector<char> randomRack(std::mt19937 &random, int size) {
    static const std::string pool = "AAAEEEIIOONNRRSSTTLLDDGUBCMPFHVWYKJXQZ??";
    std::vector<char> rack;
    for (int i = 0; i < size; ++i)
        rack.push_back(pool[random() % pool.size()]);
    return rack;
}

/** Random placements of a few rack tiles on empty squares; returns the best valid score found. */
int bestRandomScore(const Board &board, const std::vector<char> &rack, std::mt19937 &random, int attempts) {
    int best = 0;
    int found = 0;
    for (int attempt = 0; attempt < attempts && found < 50; ++attempt) {
        const int row = static_cast<int>(random() % kBoardSize);
        const int col = static_cast<int>(random() % kBoardSize);
        const bool horizontal = random() % 2 == 0;
        const int dr = horizontal ? 0 : 1;
        const int dc = horizontal ? 1 : 0;
        const int wanted = 1 + static_cast<int>(random() % rack.size());
        std::vector<Placement> tiles;
        std::vector<char> letters = rack;
        std::shuffle(letters.begin(), letters.end(), random);
        int r = row;
        int c = col;
        for (int step = 0; step < 7 && inBoard(r, c) && static_cast<int>(tiles.size()) < wanted; ++step, r += dr, c += dc) {
            if (!cellAt(board, r, c).empty())
                continue;
            const char tile = letters[tiles.size()];
            const bool blank = tile == '?';
            tiles.push_back(Placement{r, c, blank ? static_cast<char>('A' + random() % 26) : tile, blank});
        }
        if (tiles.empty())
            continue;
        const Evaluation evaluation = evaluatePlacement(board, tiles, Dawg::english());
        if (evaluation.valid) {
            ++found;
            best = std::max(best, evaluation.move.score);
        }
    }
    return best;
}

}  // namespace

class AiTests : public QObject {
    Q_OBJECT

private slots:
    void firstMoveMatchesBruteForce();
    void midGameMatchesBruteForce();
    void randomRacksAreValidAndUnique();
    void blankRackFindsBlankPlays();
    void midGameFullRackIsFast();
    void hardPicksMaximum();
    void mediumStaysNearMaximum();
    void easyIsWeakerThanHard();
    void unplayableRackHasNoMove();
    void exchangeKeepsSubMultiset();
    void levelNamesRoundTrip();
};

void AiTests::firstMoveMatchesBruteForce() {
    const std::vector<char> rack{'C', 'A', 'T', 'S', 'E', 'R', 'N'};
    const Board empty{};
    QElapsedTimer timer;
    timer.start();
    const std::vector<ScoredMove> moves = generateMoves(empty, rack, Dawg::english());
    qInfo("first move generation: %lld ms", static_cast<long long>(timer.elapsed()));
    QVERIFY(!moves.empty());

    std::set<std::string> keys;
    for (const ScoredMove &move : moves) {
        const Evaluation check = evaluatePlacement(empty, move.tiles, Dawg::english());
        QVERIFY(check.valid);
        QVERIFY(std::any_of(move.tiles.begin(), move.tiles.end(), [](const Placement &tile) {
            return tile.row == kCenterRow && tile.col == kCenterCol;
        }));
        keys.insert(keyOf(move.tiles));
    }
    QCOMPARE(keys.size(), moves.size());

    // Horizontal plays on row 7 whose first tile is at column 0..7 cover every play through the center.
    std::vector<std::pair<int, int>> starts;
    for (int c = 0; c <= kCenterCol; ++c)
        starts.emplace_back(kCenterRow, c);
    const std::vector<ScoredMove> oracle = bruteForce(empty, rack, starts, {{0, 1}});

    std::vector<ScoredMove> oracleHorizontal;
    for (const ScoredMove &move : oracle) {
        if (move.tiles.size() >= 2)
            oracleHorizontal.push_back(move);
    }
    std::vector<ScoredMove> generatedHorizontal;
    for (const ScoredMove &move : moves) {
        if (move.horizontal && move.tiles.size() >= 2)
            generatedHorizontal.push_back(move);
    }
    QVERIFY(keysOf(generatedHorizontal) == keysOf(oracleHorizontal));

    int bestOracle = 0;
    for (const ScoredMove &move : oracleHorizontal)
        bestOracle = std::max(bestOracle, move.score);
    int bestGenerated = 0;
    for (const ScoredMove &move : moves)
        bestGenerated = std::max(bestGenerated, move.score);
    QCOMPARE(bestGenerated, bestOracle);
    qInfo("first move: %zu plays, %zu horizontal oracle plays, best %d", moves.size(), oracleHorizontal.size(),
          bestGenerated);
}

void AiTests::midGameMatchesBruteForce() {
    const Board board = midGameBoard();
    std::mt19937 random(4242);
    const auto squares = allSquares();
    const std::vector<std::pair<int, int>> directions{{0, 1}, {1, 0}};
    for (int trial = 0; trial < 4; ++trial) {
        std::vector<char> rack;
        static const std::string letters = "AEIORSTNLDGMPBCUHYKWFV";
        while (rack.size() < 4) {
            const char letter = letters[random() % letters.size()];
            rack.push_back(letter);
        }
        const std::vector<ScoredMove> generated = generateMoves(board, rack, Dawg::english());
        for (const ScoredMove &move : generated)
            QVERIFY(evaluatePlacement(board, move.tiles, Dawg::english()).valid);
        QCOMPARE(keysOf(generated).size(), generated.size());

        const std::vector<ScoredMove> oracle = bruteForce(board, rack, squares, directions);
        QVERIFY(keysOf(generated) == keysOf(oracle));
    }
}

void AiTests::randomRacksAreValidAndUnique() {
    const Board board = midGameBoard();
    std::mt19937 random(777);
    for (int trial = 0; trial < 200; ++trial) {
        const std::vector<char> rack = randomRack(random, 1 + static_cast<int>(random() % kRackSize));
        const std::vector<ScoredMove> moves = generateMoves(board, rack, Dawg::english());
        std::set<std::string> keys;
        int best = 0;
        for (const ScoredMove &move : moves) {
            QVERIFY(evaluatePlacement(board, move.tiles, Dawg::english()).valid);
            keys.insert(keyOf(move.tiles));
            best = std::max(best, move.score);
        }
        QCOMPARE(keys.size(), moves.size());
        if (!moves.empty())
            QVERIFY(best >= bestRandomScore(board, rack, random, 300));
    }
}

void AiTests::blankRackFindsBlankPlays() {
    const std::vector<char> rack{'?', 'Q', 'Z', 'X', 'J', 'K', 'V'};
    const std::vector<ScoredMove> moves = generateMoves(Board{}, rack, Dawg::english());
    QVERIFY(!moves.empty());
    int blankPlays = 0;
    for (const ScoredMove &move : moves) {
        QVERIFY(evaluatePlacement(Board{}, move.tiles, Dawg::english()).valid);
        for (const Placement &tile : move.tiles) {
            if (tile.blank)
                ++blankPlays;
        }
    }
    QVERIFY(blankPlays > 0);
    qInfo("blank rack: %zu plays, %d with the blank", moves.size(), blankPlays);
}

void AiTests::midGameFullRackIsFast() {
    const Board board = midGameBoard();
    const std::vector<char> rack{'A', 'E', 'I', 'N', 'R', 'S', 'T'};
    QElapsedTimer timer;
    timer.start();
    const std::vector<ScoredMove> moves = generateMoves(board, rack, Dawg::english());
    const qint64 elapsed = timer.elapsed();
    QVERIFY(!moves.empty());
    qInfo("mid-game full rack: %zu plays in %lld ms", moves.size(), static_cast<long long>(elapsed));
    QVERIFY(elapsed < 1500);

    const std::vector<char> blankRack{'?', 'E', 'I', 'N', 'R', 'S', 'T'};
    timer.restart();
    const std::vector<ScoredMove> blankMoves = generateMoves(board, blankRack, Dawg::english());
    qInfo("mid-game blank rack: %zu plays in %lld ms", blankMoves.size(), static_cast<long long>(timer.elapsed()));
    QVERIFY(!blankMoves.empty());
}

void AiTests::hardPicksMaximum() {
    const Board board = midGameBoard();
    const std::vector<char> rack{'A', 'E', 'I', 'N', 'R', 'S', 'T'};
    const std::vector<ScoredMove> moves = generateMoves(board, rack, Dawg::english());
    int best = 0;
    for (const ScoredMove &move : moves)
        best = std::max(best, move.score);
    std::mt19937 random(1);
    const auto chosen = chooseMove(board, rack, Dawg::english(), Level::Hard, random);
    QVERIFY(chosen.has_value());
    QCOMPARE(chosen->score, best);
}

void AiTests::mediumStaysNearMaximum() {
    const Board board = midGameBoard();
    const std::vector<char> rack{'A', 'E', 'I', 'N', 'R', 'S', 'T'};
    const std::vector<ScoredMove> moves = generateMoves(board, rack, Dawg::english());
    int best = 0;
    for (const ScoredMove &move : moves)
        best = std::max(best, move.score);
    std::mt19937 random(2);
    for (int draw = 0; draw < 200; ++draw) {
        const auto chosen = chooseMove(board, rack, Dawg::english(), Level::Medium, random);
        QVERIFY(chosen.has_value());
        QVERIFY(chosen->score * 5 >= best * 3);
        QVERIFY(chosen->score <= best);
    }
}

void AiTests::easyIsWeakerThanHard() {
    const Board board = midGameBoard();
    const std::vector<char> rack{'A', 'E', 'I', 'N', 'R', 'S', 'T'};
    const std::vector<ScoredMove> moves = generateMoves(board, rack, Dawg::english());
    int best = 0;
    for (const ScoredMove &move : moves)
        best = std::max(best, move.score);
    std::mt19937 random(3);
    long long total = 0;
    for (int draw = 0; draw < 200; ++draw) {
        const auto chosen = chooseMove(board, rack, Dawg::english(), Level::Easy, random);
        QVERIFY(chosen.has_value());
        QVERIFY(chosen->score <= best);
        total += chosen->score;
    }
    QVERIFY(static_cast<double>(total) / 200.0 < static_cast<double>(best));
}

void AiTests::unplayableRackHasNoMove() {
    std::mt19937 random(4);
    const std::vector<char> rack{'Q'};
    QVERIFY(!chooseMove(Board{}, rack, Dawg::english(), Level::Hard, random).has_value());
    QVERIFY(!chooseMove(Board{}, rack, Dawg::english(), Level::Easy, random).has_value());
}

void AiTests::exchangeKeepsSubMultiset() {
    const std::vector<std::vector<char>> racks{
        {'A', 'B', 'C', 'D', 'E', 'F', 'G'}, {'?', 'Q', 'Z', 'X', 'J', 'K', 'V'}, {'E', 'E', 'S', 'S', 'R', 'T', 'A'},
        {'S', 'E'}, {'Q'}};
    for (const auto &rack : racks) {
        for (Level level : {Level::Easy, Level::Medium, Level::Hard}) {
            const std::vector<char> exchange = chooseExchange(rack, level);
            QVERIFY(!exchange.empty());
            std::vector<char> remaining = rack;
            for (char tile : exchange) {
                const auto it = std::find(remaining.begin(), remaining.end(), tile);
                QVERIFY(it != remaining.end());
                remaining.erase(it);
            }
        }
    }
    QVERIFY((chooseExchange({'Q'}, Level::Hard) == std::vector<char>{'Q'}));
}

void AiTests::levelNamesRoundTrip() {
    for (Level level : {Level::Easy, Level::Medium, Level::Hard})
        QVERIFY(parseLevel(levelName(level)) == level);
    QVERIFY(parseLevel("HARD") == Level::Hard);
    QVERIFY(parseLevel("nonsense") == Level::Medium);
    QVERIFY(std::string(levelName(Level::Easy)) == "easy");
}

QTEST_GUILESS_MAIN(AiTests)
#include "ai_tests.moc"
