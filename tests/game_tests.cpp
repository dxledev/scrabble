/**
 * @file game_tests.cpp
 * @brief Rules, move evaluation, game flow, and serialization tests.
 */
#include "core/dawg.h"
#include "core/game.h"
#include "core/rules.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <algorithm>
#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

using namespace scrabble;

namespace {

Placement at(int row, int col, char letter, bool blank = false) {
    return Placement{row, col, letter, blank};
}

QString compact(const QJsonObject &object) {
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

// Every tile of a full bag, with blanks as '?'.
std::string fullDistribution() {
    std::string tiles;
    for (char tile = 'A'; tile <= 'Z'; ++tile)
        tiles.append(static_cast<std::size_t>(tileCount(tile)), tile);
    tiles.append(static_cast<std::size_t>(tileCount('?')), '?');
    return tiles;
}

// State JSON whose board holds the first @p boardCount tiles left after the racks; the rest go to the bag.
// Blanks on the board are written as lowercase 'e'. Racks must be full whenever the bag is not empty.
QJsonObject makeState(int boardCount, const std::string &rack0, const std::string &rack1) {
    std::string pool = fullDistribution();
    for (char tile : rack0 + rack1) {
        const auto found = pool.find(tile);
        if (found != std::string::npos)
            pool.erase(found, 1);
    }
    std::string board(225, '.');
    const auto boardTiles = static_cast<std::size_t>(boardCount);
    for (std::size_t i = 0; i < boardTiles; ++i) {
        const char tile = pool[i];
        board[i] = tile == '?' ? 'e' : tile;
    }
    const std::string bag = pool.substr(boardTiles);

    QJsonObject state;
    state[QStringLiteral("version")] = 1;
    state[QStringLiteral("board")] = QString::fromStdString(board);
    state[QStringLiteral("racks")] = QJsonArray{QString::fromStdString(rack0), QString::fromStdString(rack1)};
    state[QStringLiteral("bag")] = QString::fromStdString(bag);
    state[QStringLiteral("scores")] = QJsonArray{0, 0};
    state[QStringLiteral("turn")] = 0;
    state[QStringLiteral("scoreless")] = 0;
    state[QStringLiteral("finished")] = false;
    state[QStringLiteral("rngState")] = 1;
    state[QStringLiteral("log")] = QJsonArray();
    state[QStringLiteral("lastPlay")] = QJsonArray();
    return state;
}

// Board, both racks and bag together must hold exactly the full distribution.
bool conserved(const Game &game) {
    std::array<int, 27> counts = game.unseenCounts(Game::kHuman);
    for (char tile : game.rack(Game::kHuman))
        ++counts[tile == '?' ? 26 : tile - 'A'];
    for (const Cell &cell : game.board()) {
        if (!cell.empty())
            ++counts[cell.blank ? 26 : cell.letter - 'A'];
    }
    for (char tile = 'A'; tile <= 'Z'; ++tile) {
        if (counts[tile - 'A'] != tileCount(tile))
            return false;
    }
    return counts[26] == tileCount('?');
}

char missingLetter(const std::vector<char> &rack) {
    for (char tile = 'A'; tile <= 'Z'; ++tile) {
        if (std::find(rack.begin(), rack.end(), tile) == rack.end())
            return tile;
    }
    return 'A';
}

int handValue(const std::vector<char> &rack) {
    int sum = 0;
    for (char tile : rack)
        sum += letterValue(tile);
    return sum;
}

}

class GameTests : public QObject {
    Q_OBJECT

private:
    std::vector<std::uint32_t> edges_;
    Dawg dict_;

    // Places the first play QAT on row 7, columns 6-8 (Q at (7,6), A at (7,7), T at (7,8)).
    void placeQat(Board &board) const {
        const Evaluation first = evaluatePlacement(Board{}, {at(7, 6, 'Q'), at(7, 7, 'A'), at(7, 8, 'T')}, dict_);
        QVERIFY(first.valid);
        for (const Placement &tile : first.move.tiles)
            cellAt(board, tile.row, tile.col) = Cell{tile.letter, tile.blank};
    }

    Board qatBoard() const {
        Board board{};
        placeQat(board);
        return board;
    }

    static bool hasWords(const std::vector<std::string> &actual, const std::vector<std::string> &expected) {
        return actual == expected;
    }

private slots:
    void initTestCase() {
        edges_ = buildDawgEdges({"CAT", "QAT", "TAX", "XI", "AX", "TI", "RAINBOW"});
        dict_ = Dawg(edges_.data(), edges_.size());
        QVERIFY(dict_.valid());
    }

    void premiumLayout() {
        QVERIFY(premiumAt(0, 0) == Premium::TripleWord);
        QVERIFY(premiumAt(7, 7) == Premium::DoubleWord);
        QVERIFY(premiumAt(1, 5) == Premium::TripleLetter);
        QVERIFY(premiumAt(0, 3) == Premium::DoubleLetter);
        QVERIFY(premiumAt(7, 0) == Premium::TripleWord);
        QVERIFY(premiumAt(-1, 0) == Premium::None);

        int tw = 0;
        int dw = 0;
        int tl = 0;
        int dl = 0;
        for (int row = 0; row < kBoardSize; ++row) {
            for (int col = 0; col < kBoardSize; ++col) {
                switch (premiumAt(row, col)) {
                case Premium::TripleWord: ++tw; break;
                case Premium::DoubleWord: ++dw; break;
                case Premium::TripleLetter: ++tl; break;
                case Premium::DoubleLetter: ++dl; break;
                case Premium::None: break;
                }
                QVERIFY(premiumAt(row, col) == premiumAt(col, row));
                QVERIFY(premiumAt(row, col) == premiumAt(kBoardSize - 1 - row, kBoardSize - 1 - col));
            }
        }
        QCOMPARE(tw, 8);
        QCOMPARE(dw, 17);
        QCOMPARE(tl, 12);
        QCOMPARE(dl, 24);
    }

    void tileValuesAndDistribution() {
        QCOMPARE(letterValue('Q'), 10);
        QCOMPARE(letterValue('A'), 1);
        QCOMPARE(letterValue('?'), 0);
        int total = tileCount('?');
        for (char tile = 'A'; tile <= 'Z'; ++tile)
            total += tileCount(tile);
        QCOMPARE(total, kTileTotal);
        QCOMPARE(tileCount('?'), 2);
    }

    void firstMoveMustCoverCenter() {
        const Evaluation off = evaluatePlacement(Board{}, {at(0, 0, 'Q'), at(0, 1, 'A')}, dict_);
        QVERIFY(!off.valid);
        QCOMPARE(QString::fromStdString(off.error), QStringLiteral("First word must cover the center square"));

        const Evaluation single = evaluatePlacement(Board{}, {at(7, 7, 'A')}, dict_);
        QVERIFY(!single.valid);
        QCOMPARE(QString::fromStdString(single.error), QStringLiteral("First word needs at least two tiles"));
    }

    void classicScoreOnEmptyBoard() {
        const Evaluation eval = evaluatePlacement(Board{}, {at(7, 6, 'C'), at(7, 7, 'A'), at(7, 8, 'T')}, dict_);
        QVERIFY(eval.valid);
        // (3 + 1 + 1) doubled by the center double word.
        QCOMPARE(eval.move.score, 10);
        QVERIFY(hasWords(eval.move.words, {"CAT"}));
        QVERIFY(eval.move.horizontal);
        QVERIFY(!eval.move.bingo);
        QVERIFY(eval.move.tiles.front() == at(7, 6, 'C'));
    }

    void existingTilesDoNotRepeatPremiums() {
        // TAX down column 7 through the A on the center; the double word was already used by QAT.
        Board board{};
        placeQat(board);
        const Evaluation eval = evaluatePlacement(board, {at(6, 7, 'T'), at(8, 7, 'X')}, dict_);
        QVERIFY(eval.valid);
        QCOMPARE(eval.move.score, 10);
        QVERIFY(hasWords(eval.move.words, {"TAX"}));
        QVERIFY(!eval.move.horizontal);
    }

    void crossWordsAndNewPremiums() {
        // XI on row 8: X at (8,7), I on the double letter (8,8). Cross words AX and TI.
        Board board{};
        placeQat(board);
        const Evaluation eval = evaluatePlacement(board, {at(8, 7, 'X'), at(8, 8, 'I')}, dict_);
        QVERIFY(eval.valid);
        // XI = 8 + 1*2, AX = 1 + 8, TI = 1 + 1*2.
        QCOMPARE(eval.move.score, 22);
        QVERIFY(hasWords(eval.move.words, {"XI", "AX", "TI"}));
    }

    void blankScoresZeroButStillCountsAsLetter() {
        Board board{};
        placeQat(board);
        const Evaluation eval = evaluatePlacement(board, {at(8, 7, 'X'), at(8, 8, 'I', true)}, dict_);
        QVERIFY(eval.valid);
        // XI = 8 + 0, AX = 9, TI = 1 + 0.
        QCOMPARE(eval.move.score, 18);
        QVERIFY(hasWords(eval.move.words, {"XI", "AX", "TI"}));
    }

    void bingoBonus() {
        const Evaluation eval = evaluatePlacement(Board{},
            {at(7, 4, 'R'), at(7, 5, 'A'), at(7, 6, 'I'), at(7, 7, 'N'), at(7, 8, 'B'), at(7, 9, 'O'), at(7, 10, 'W')},
            dict_);
        QVERIFY(eval.valid);
        QVERIFY(eval.move.bingo);
        // Letters sum to 12 and the center doubles them; the bingo bonus is added on top.
        QCOMPARE(eval.move.score, 24 + kBingoBonus);
        QVERIFY(hasWords(eval.move.words, {"RAINBOW"}));
    }

    void rejectsGap() {
        const Board board = qatBoard();
        const Evaluation eval = evaluatePlacement(board, {at(8, 9, 'X'), at(8, 11, 'I')}, dict_);
        QVERIFY(!eval.valid);
        QCOMPARE(QString::fromStdString(eval.error), QStringLiteral("Tiles must form one continuous word"));
    }

    void rejectsNotOneLine() {
        const Board board = qatBoard();
        const Evaluation eval = evaluatePlacement(board, {at(8, 8, 'X'), at(9, 9, 'I')}, dict_);
        QVERIFY(!eval.valid);
        QCOMPARE(QString::fromStdString(eval.error), QStringLiteral("Tiles must be in one row or column"));
    }

    void rejectsOccupiedSquare() {
        const Board board = qatBoard();
        const Evaluation eval = evaluatePlacement(board, {at(7, 7, 'X')}, dict_);
        QVERIFY(!eval.valid);
        QCOMPARE(QString::fromStdString(eval.error), QStringLiteral("Square is already occupied"));
    }

    void rejectsDisconnectedPlay() {
        const Board board = qatBoard();
        const Evaluation eval = evaluatePlacement(board, {at(10, 10, 'X'), at(10, 11, 'I')}, dict_);
        QVERIFY(!eval.valid);
        QCOMPARE(QString::fromStdString(eval.error), QStringLiteral("Word must connect to existing tiles"));
    }

    void rejectsUnknownWordByName() {
        // XQ on row 8 is the main word; AX is in the dictionary but TQ is not.
        const Board board = qatBoard();
        const Evaluation eval = evaluatePlacement(board, {at(8, 7, 'X'), at(8, 8, 'Q')}, dict_);
        QVERIFY(!eval.valid);
        QCOMPARE(QString::fromStdString(eval.error), QStringLiteral("XQ is not in the dictionary"));
    }

    void rejectsMalformedTiles() {
        QVERIFY(!evaluatePlacement(Board{}, {}, dict_).valid);
        QVERIFY(!evaluatePlacement(Board{}, {at(7, 7, 'A'), at(7, 15, 'T')}, dict_).valid);
        QVERIFY(!evaluatePlacement(Board{}, {at(7, 7, 'a'), at(7, 8, 'T')}, dict_).valid);
        QVERIFY(!evaluatePlacement(Board{}, {at(7, 7, 'A'), at(7, 7, 'T')}, dict_).valid);
    }

    void newGameDealsSevenEach() {
        const Game game(7);
        QCOMPARE(game.rack(Game::kHuman).size(), std::size_t(kRackSize));
        QCOMPARE(game.rack(Game::kComputer).size(), std::size_t(kRackSize));
        QCOMPARE(game.bagSize(), kTileTotal - 2 * kRackSize);
        QCOMPARE(game.turn(), Game::kHuman);
        QCOMPARE(game.score(Game::kHuman), 0);
        QVERIFY(!game.finished());
        QVERIFY(conserved(game));
        QVERIFY(game.canExchange());
    }

    void sameSeedSameRacks() {
        const Game a(42);
        const Game b(42);
        QVERIFY(a.rack(Game::kHuman) == b.rack(Game::kHuman));
        QVERIFY(a.rack(Game::kComputer) == b.rack(Game::kComputer));
        QCOMPARE(compact(a.toJson()), compact(b.toJson()));
    }

    void playConsumesAndAlternates() {
        Game game(5);
        const std::vector<char> before = game.rack(Game::kHuman);
        ScoredMove move;
        move.tiles = {at(7, 7, before.front())};
        move.score = 3;
        move.words = {std::string(2, before.front())};
        QVERIFY(game.play(move));
        QVERIFY(game.board()[7 * kBoardSize + 7].letter == before.front());
        QCOMPARE(game.score(Game::kHuman), 3);
        QCOMPARE(game.rack(Game::kHuman).size(), std::size_t(kRackSize));
        QCOMPARE(game.bagSize(), kTileTotal - 2 * kRackSize - 1);
        QCOMPARE(game.turn(), Game::kComputer);
        QCOMPARE(game.lastPlay().size(), std::size_t(1));
        QCOMPARE(game.log().size(), std::size_t(1));
        QVERIFY(game.log().front().kind == TurnKind::Play);
        QVERIFY(game.log().front().player == Game::kHuman);
        QVERIFY(conserved(game));

        // A tile the mover does not hold must fail and leave the state untouched.
        const QString snapshot = compact(game.toJson());
        ScoredMove bad;
        bad.tiles = {at(0, 0, missingLetter(game.rack(Game::kHuman)))};
        QVERIFY(!game.play(bad));
        QCOMPARE(compact(game.toJson()), snapshot);
    }

    void sixPassesEndGame() {
        Game game(1);
        const int humanLeft = handValue(game.rack(Game::kHuman));
        const int computerLeft = handValue(game.rack(Game::kComputer));
        for (int i = 0; i < 5; ++i) {
            QVERIFY(game.pass());
            QVERIFY(!game.finished());
        }
        QVERIFY(game.pass());
        QVERIFY(game.finished());
        QCOMPARE(game.scorelessTurns(), 6);
        QCOMPARE(game.score(Game::kHuman), -humanLeft);
        QCOMPARE(game.score(Game::kComputer), -computerLeft);
        QVERIFY(!game.pass());
        QVERIFY(!game.canExchange());
        QVERIFY(conserved(game));
    }

    void exchangeNeedsBagAndKeepsRackSize() {
        Game game(7);
        const std::vector<char> rack = game.rack(Game::kHuman);
        QVERIFY(game.exchange({rack[0], rack[1]}));
        QCOMPARE(game.rack(Game::kHuman).size(), std::size_t(kRackSize));
        QCOMPARE(game.bagSize(), kTileTotal - 2 * kRackSize);
        QCOMPARE(game.turn(), Game::kComputer);
        QVERIFY(game.log().back().kind == TurnKind::Exchange);
        QVERIFY(game.log().back().text == "2");
        QVERIFY(conserved(game));

        QVERIFY(!game.exchange({missingLetter(game.rack(Game::kComputer))}));
        QVERIFY(!game.exchange({}));
    }

    void exchangeRejectedWhenBagTooSmall() {
        const auto parsed = Game::fromJson(makeState(80, "AEIOUST", "BCDFGHK"));
        QVERIFY(parsed.has_value());
        Game game = *parsed;
        QCOMPARE(game.bagSize(), 6);
        QVERIFY(!game.canExchange());
        QVERIFY(!game.exchange({game.rack(Game::kHuman).front()}));
        QCOMPARE(game.turn(), Game::kHuman);
    }

    void endgameAddsOpponentRack() {
        // Human holds only Z, computer holds Q and A, and the bag is empty.
        const auto parsed = Game::fromJson(makeState(97, "Z", "QA"));
        QVERIFY(parsed.has_value());
        Game game = *parsed;
        ScoredMove last;
        last.tiles = {at(13, 5, 'Z')};
        last.score = 5;
        last.words = {"ZA"};
        QVERIFY(game.play(last));
        QVERIFY(game.finished());
        QCOMPARE(game.score(Game::kHuman), 5 + letterValue('Q') + letterValue('A'));
        QCOMPARE(game.score(Game::kComputer), -(letterValue('Q') + letterValue('A')));
        QCOMPARE(game.winner(), Game::kHuman);
        QVERIFY(conserved(game));
    }

    void roundTripIsStable() {
        Game game(9);
        ScoredMove move;
        move.tiles = {at(7, 7, game.rack(Game::kHuman).front())};
        move.score = 4;
        move.words = {std::string(1, move.tiles.front().letter)};
        QVERIFY(game.play(move));
        QVERIFY(game.pass());

        const QJsonObject saved = game.toJson();
        const auto restored = Game::fromJson(saved);
        QVERIFY(restored.has_value());
        QCOMPARE(compact(restored->toJson()), compact(saved));
        QCOMPARE(restored->turn(), game.turn());
        QVERIFY(restored->lastPlay().empty());
    }

    void fromJsonRejectsBrokenState() {
        const QJsonObject valid = makeState(97, "Z", "QA");
        QVERIFY(Game::fromJson(valid).has_value());

        QJsonObject missing = valid;
        std::string board = missing[QStringLiteral("board")].toString().toStdString();
        board[0] = '.';
        missing[QStringLiteral("board")] = QString::fromStdString(board);
        QVERIFY(!Game::fromJson(missing).has_value());

        QJsonObject extra = valid;
        board = extra[QStringLiteral("board")].toString().toStdString();
        board[200] = 'A';
        extra[QStringLiteral("board")] = QString::fromStdString(board);
        QVERIFY(!Game::fromJson(extra).has_value());

        QJsonObject badChar = valid;
        board = badChar[QStringLiteral("board")].toString().toStdString();
        board[0] = '#';
        badChar[QStringLiteral("board")] = QString::fromStdString(board);
        QVERIFY(!Game::fromJson(badChar).has_value());

        QJsonObject shortBoard = valid;
        shortBoard[QStringLiteral("board")] = QString::fromStdString(board.substr(1));
        QVERIFY(!Game::fromJson(shortBoard).has_value());

        QJsonObject lowerRack = valid;
        lowerRack[QStringLiteral("racks")] = QJsonArray{QStringLiteral("z"), QStringLiteral("QA")};
        QVERIFY(!Game::fromJson(lowerRack).has_value());

        QJsonObject longRack = valid;
        longRack[QStringLiteral("racks")] = QJsonArray{QStringLiteral("ZZZZZZZZ"), QStringLiteral("QA")};
        QVERIFY(!Game::fromJson(longRack).has_value());

        QJsonObject badTurn = valid;
        badTurn[QStringLiteral("turn")] = 2;
        QVERIFY(!Game::fromJson(badTurn).has_value());

        QJsonObject badScoreless = valid;
        badScoreless[QStringLiteral("scoreless")] = 7;
        QVERIFY(!Game::fromJson(badScoreless).has_value());

        QJsonObject wrongType = valid;
        wrongType[QStringLiteral("board")] = 12;
        QVERIFY(!Game::fromJson(wrongType).has_value());

        QJsonObject wrongVersion = valid;
        wrongVersion[QStringLiteral("version")] = 2;
        QVERIFY(!Game::fromJson(wrongVersion).has_value());

        QJsonObject fractional = valid;
        fractional[QStringLiteral("scores")] = QJsonArray{0.5, 0};
        QVERIFY(!Game::fromJson(fractional).has_value());

        QJsonObject zeroRng = valid;
        zeroRng[QStringLiteral("rngState")] = 0;
        QVERIFY(!Game::fromJson(zeroRng).has_value());

        QJsonObject hugeLog = valid;
        QJsonArray entries;
        for (int i = 0; i < 20000; ++i)
            entries.append(QJsonObject{});
        hugeLog[QStringLiteral("log")] = entries;
        QVERIFY(!Game::fromJson(hugeLog).has_value());
    }

    void randomSelfPlaySoak() {
        std::mt19937 choice(123);
        Game game(1);
        for (int step = 0; step < 200; ++step) {
            if (game.finished())
                game = Game(static_cast<std::uint32_t>(step + 2));
            const std::vector<char> rack = game.rack(game.turn());
            if (game.canExchange() && choice() % 3 == 0)
                QVERIFY(game.exchange({rack[choice() % rack.size()]}));
            else
                QVERIFY(game.pass());
            QVERIFY(conserved(game));
        }
    }
};

QTEST_APPLESS_MAIN(GameTests)
#include "game_tests.moc"
