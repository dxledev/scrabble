/**
 * @file game.cpp
 * @brief Move validation and scoring, the two-player game state machine, and its JSON form.
 */
#include "game.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QLatin1StringView>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>

namespace scrabble {
namespace {

constexpr std::uint32_t kDefaultSeed = 0x9E3779B9u;
constexpr int kSquares = kBoardSize * kBoardSize;
constexpr int kCenter = (kBoardSize / 2) * kBoardSize + kBoardSize / 2;
constexpr int kScorelessLimit = 6;
constexpr int kBlankIndex = 26;
constexpr int kTileKinds = 27;
constexpr int kMaxScore = 1000000;
constexpr int kMaxLogEntries = 10000;
constexpr int kMaxLastPlay = kRackSize;

bool inBounds(int row, int col) {
    return row >= 0 && row < kBoardSize && col >= 0 && col < kBoardSize;
}

bool isTile(char tile) {
    return (tile >= 'A' && tile <= 'Z') || tile == '?';
}

int tileIndex(char tile) {
    return tile == '?' ? kBlankIndex : tile - 'A';
}

// Removes one copy of @p tile from @p rack; false when the rack does not hold it.
bool takeTile(std::vector<char> &rack, char tile) {
    const auto it = std::find(rack.begin(), rack.end(), tile);
    if (it == rack.end())
        return false;
    rack.erase(it);
    return true;
}

// xorshift32; @p state must be nonzero.
std::uint32_t nextRandom(std::uint32_t &state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

// Fisher-Yates shuffle driven by the game's own generator so saved games replay identically.
void shuffleBag(std::vector<char> &bag, std::uint32_t &state) {
    for (std::size_t i = bag.size(); i > 1; --i) {
        const std::size_t j = nextRandom(state) % i;
        std::swap(bag[i - 1], bag[j]);
    }
}

// Occupied run through a square along one direction; (row, col) is its first square.
struct Run {
    int row = 0;
    int col = 0;
    int length = 0;
};

Run runThrough(const Board &board, int row, int col, int dr, int dc) {
    int r = row;
    int c = col;
    while (inBounds(r - dr, c - dc) && !cellAt(board, r - dr, c - dc).empty()) {
        r -= dr;
        c -= dc;
    }
    Run run{r, c, 0};
    for (int rr = r, cc = c; inBounds(rr, cc) && !cellAt(board, rr, cc).empty(); rr += dr, cc += dc)
        ++run.length;
    return run;
}

std::string wordOf(const Board &board, const Run &run, int dr, int dc) {
    std::string word;
    for (int i = 0; i < run.length; ++i)
        word += cellAt(board, run.row + i * dr, run.col + i * dc).letter;
    return word;
}

// Letter and word premiums apply only to squares in @p fresh (tiles placed this turn).
int scoreOf(const Board &board, const std::array<bool, kSquares> &fresh, const Run &run, int dr, int dc) {
    int sum = 0;
    int wordMultiplier = 1;
    for (int i = 0; i < run.length; ++i) {
        const int row = run.row + i * dr;
        const int col = run.col + i * dc;
        const Cell &cell = cellAt(board, row, col);
        int value = cell.blank ? 0 : letterValue(cell.letter);
        if (fresh[row * kBoardSize + col]) {
            switch (premiumAt(row, col)) {
            case Premium::DoubleLetter: value *= 2; break;
            case Premium::TripleLetter: value *= 3; break;
            case Premium::DoubleWord: wordMultiplier *= 2; break;
            case Premium::TripleWord: wordMultiplier *= 3; break;
            case Premium::None: break;
            }
        }
        sum += value;
    }
    return sum * wordMultiplier;
}

bool touchesExisting(const Board &board, int row, int col) {
    constexpr int kSteps[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (const auto &step : kSteps) {
        const int r = row + step[0];
        const int c = col + step[1];
        if (inBounds(r, c) && !cellAt(board, r, c).empty())
            return true;
    }
    return false;
}

char boardChar(const Cell &cell) {
    if (cell.empty())
        return '.';
    return cell.blank ? static_cast<char>(cell.letter - 'A' + 'a') : cell.letter;
}

const char *kindName(TurnKind kind) {
    switch (kind) {
    case TurnKind::Play: return "play";
    case TurnKind::Pass: return "pass";
    case TurnKind::Exchange: return "exchange";
    }
    return "play";
}

std::optional<qint64> integerValue(const QJsonValue &value, qint64 low, qint64 high) {
    if (!value.isDouble())
        return std::nullopt;
    const double number = value.toDouble();
    if (number != std::floor(number) || number < static_cast<double>(low) || number > static_cast<double>(high))
        return std::nullopt;
    return static_cast<qint64>(number);
}

std::optional<int> intField(const QJsonObject &object, const char *key, int low, int high) {
    const auto value = integerValue(object.value(QLatin1StringView(key)), low, high);
    if (!value)
        return std::nullopt;
    return static_cast<int>(*value);
}

std::optional<QJsonArray> arrayField(const QJsonObject &object, const char *key, int maxSize) {
    const QJsonValue value = object.value(QLatin1StringView(key));
    if (!value.isArray() || value.toArray().size() > maxSize)
        return std::nullopt;
    return value.toArray();
}

// Tile string of at most @p maxCount characters, each 'A'-'Z' or '?'.
std::optional<std::vector<char>> tilesOf(const QJsonValue &value, std::size_t maxCount) {
    if (!value.isString())
        return std::nullopt;
    const std::string text = value.toString().toStdString();
    if (text.size() > maxCount)
        return std::nullopt;
    for (char tile : text)
        if (!isTile(tile))
            return std::nullopt;
    return std::vector<char>(text.begin(), text.end());
}

}

Evaluation evaluatePlacement(const Board &board, const std::vector<Placement> &tiles, const Dawg &dictionary) {
    Evaluation result;
    const auto reject = [&result](std::string message) {
        result.valid = false;
        result.error = std::move(message);
        return result;
    };
    if (tiles.empty())
        return reject("Place at least one tile");
    if (tiles.size() > static_cast<std::size_t>(kRackSize))
        return reject("Too many tiles");

    Board work = board;
    std::array<bool, kSquares> fresh{};
    for (const Placement &tile : tiles) {
        if (!inBounds(tile.row, tile.col))
            return reject("Tile is off the board");
        if (tile.letter < 'A' || tile.letter > 'Z')
            return reject("Invalid tile letter");
        if (!cellAt(board, tile.row, tile.col).empty())
            return reject("Square is already occupied");
        const int index = tile.row * kBoardSize + tile.col;
        if (fresh[index])
            return reject("Duplicate square");
        fresh[index] = true;
        cellAt(work, tile.row, tile.col) = Cell{tile.letter, tile.blank};
    }

    std::vector<Placement> sorted = tiles;
    std::sort(sorted.begin(), sorted.end(), [](const Placement &a, const Placement &b) {
        return a.row != b.row ? a.row < b.row : a.col < b.col;
    });
    const Placement &lo = sorted.front();
    const Placement &hi = sorted.back();
    const bool sameRow = lo.row == hi.row;
    const bool sameCol = lo.col == hi.col;
    if (!sameRow && !sameCol)
        return reject("Tiles must be in one row or column");

    const bool boardEmpty = std::all_of(board.begin(), board.end(), [](const Cell &cell) { return cell.empty(); });
    if (boardEmpty) {
        if (tiles.size() < 2)
            return reject("First word needs at least two tiles");
        if (!fresh[kCenter])
            return reject("First word must cover the center square");
    }

    bool horizontal = sameRow;
    if (tiles.size() == 1)
        horizontal = runThrough(work, lo.row, lo.col, 0, 1).length >= runThrough(work, lo.row, lo.col, 1, 0).length;
    const int dr = horizontal ? 0 : 1;
    const int dc = horizontal ? 1 : 0;
    const int pr = horizontal ? 1 : 0;
    const int pc = horizontal ? 0 : 1;

    const int span = horizontal ? hi.col - lo.col : hi.row - lo.row;
    for (int k = 0; k <= span; ++k) {
        if (cellAt(work, lo.row + k * dr, lo.col + k * dc).empty())
            return reject("Tiles must form one continuous word");
    }

    if (!boardEmpty && !std::any_of(sorted.begin(), sorted.end(), [&board](const Placement &tile) {
            return touchesExisting(board, tile.row, tile.col);
        })) {
        return reject("Word must connect to existing tiles");
    }

    const Run mainRun = runThrough(work, lo.row, lo.col, dr, dc);
    std::vector<std::string> words{wordOf(work, mainRun, dr, dc)};
    int score = scoreOf(work, fresh, mainRun, dr, dc);
    for (const Placement &tile : sorted) {
        const Run cross = runThrough(work, tile.row, tile.col, pr, pc);
        if (cross.length < 2)
            continue;
        words.push_back(wordOf(work, cross, pr, pc));
        score += scoreOf(work, fresh, cross, pr, pc);
    }
    for (const std::string &word : words) {
        if (!dictionary.contains(word))
            return reject(word + " is not in the dictionary");
    }

    const bool bingo = tiles.size() == static_cast<std::size_t>(kRackSize);
    if (bingo)
        score += kBingoBonus;

    result.valid = true;
    result.move.tiles = std::move(sorted);
    result.move.score = score;
    result.move.horizontal = horizontal;
    result.move.bingo = bingo;
    result.move.words = std::move(words);
    return result;
}

Game::Game(std::uint32_t seed) : rngState_(seed != 0 ? seed : kDefaultSeed) {
    for (char tile = 'A'; tile <= 'Z'; ++tile)
        for (int n = tileCount(tile); n > 0; --n)
            bag_.push_back(tile);
    for (int n = tileCount('?'); n > 0; --n)
        bag_.push_back('?');
    shuffleBag(bag_, rngState_);
    refill(kHuman);
    refill(kComputer);
}

int Game::winner() const {
    if (scores_[kHuman] == scores_[kComputer])
        return -1;
    return scores_[kHuman] > scores_[kComputer] ? kHuman : kComputer;
}

std::array<int, kTileKinds> Game::unseenCounts(int player) const {
    std::array<int, kTileKinds> counts{};
    for (char tile : bag_)
        ++counts[tileIndex(tile)];
    for (char tile : racks_[1 - player])
        ++counts[tileIndex(tile)];
    return counts;
}

bool Game::play(const ScoredMove &move) {
    if (finished_ || move.tiles.empty())
        return false;
    std::vector<char> rack = racks_[turn_];
    for (std::size_t i = 0; i < move.tiles.size(); ++i) {
        const Placement &tile = move.tiles[i];
        if (!inBounds(tile.row, tile.col) || !cellAt(board_, tile.row, tile.col).empty())
            return false;
        if (tile.letter < 'A' || tile.letter > 'Z')
            return false;
        for (std::size_t j = 0; j < i; ++j) {
            if (move.tiles[j].row == tile.row && move.tiles[j].col == tile.col)
                return false;
        }
        if (!takeTile(rack, tile.blank ? '?' : tile.letter))
            return false;
    }

    for (const Placement &tile : move.tiles)
        cellAt(board_, tile.row, tile.col) = Cell{tile.letter, tile.blank};
    racks_[turn_] = std::move(rack);
    scores_[turn_] += move.score;
    refill(turn_);
    lastPlay_ = move.tiles;
    log_.push_back({turn_, TurnKind::Play, move.score, move.words.empty() ? std::string() : move.words.front()});
    scoreless_ = 0;
    if (racks_[turn_].empty() && bag_.empty())
        finishGame(turn_);
    else
        turn_ = 1 - turn_;
    return true;
}

bool Game::pass() {
    if (finished_)
        return false;
    log_.push_back({turn_, TurnKind::Pass, 0, std::string()});
    lastPlay_.clear();
    ++scoreless_;
    if (scoreless_ >= kScorelessLimit)
        finishGame(-1);
    else
        turn_ = 1 - turn_;
    return true;
}

bool Game::exchange(const std::vector<char> &tiles) {
    if (!canExchange() || tiles.empty())
        return false;
    std::vector<char> rack = racks_[turn_];
    for (char tile : tiles)
        if (!takeTile(rack, tile))
            return false;

    // Replacements are drawn before the returned tiles re-enter the bag, as the standard rule requires.
    racks_[turn_] = std::move(rack);
    refill(turn_);
    bag_.insert(bag_.end(), tiles.begin(), tiles.end());
    shuffleBag(bag_, rngState_);

    log_.push_back({turn_, TurnKind::Exchange, 0, std::to_string(tiles.size())});
    lastPlay_.clear();
    ++scoreless_;
    if (scoreless_ >= kScorelessLimit)
        finishGame(-1);
    else
        turn_ = 1 - turn_;
    return true;
}

void Game::refill(int player) {
    auto &rack = racks_[player];
    while (static_cast<int>(rack.size()) < kRackSize && !bag_.empty()) {
        rack.push_back(bag_.back());
        bag_.pop_back();
    }
}

void Game::finishGame(int emptiedPlayer) {
    finished_ = true;
    std::array<int, 2> leftover{};
    for (int player = 0; player < 2; ++player)
        for (char tile : racks_[player])
            leftover[player] += letterValue(tile);
    for (int player = 0; player < 2; ++player)
        scores_[player] -= leftover[player];
    if (emptiedPlayer >= 0)
        scores_[emptiedPlayer] += leftover[1 - emptiedPlayer];
}

QJsonObject Game::toJson() const {
    std::string boardText;
    boardText.reserve(kSquares);
    for (const Cell &cell : board_)
        boardText += boardChar(cell);

    QJsonArray racks;
    for (const auto &rack : racks_)
        racks.append(QString::fromStdString(std::string(rack.begin(), rack.end())));

    QJsonArray log;
    for (const LogEntry &entry : log_) {
        QJsonObject item;
        item[QStringLiteral("player")] = entry.player;
        item[QStringLiteral("kind")] = QString::fromLatin1(kindName(entry.kind));
        item[QStringLiteral("score")] = entry.score;
        item[QStringLiteral("text")] = QString::fromStdString(entry.text);
        log.append(item);
    }

    QJsonArray lastPlay;
    for (const Placement &tile : lastPlay_) {
        QJsonObject item;
        item[QStringLiteral("row")] = tile.row;
        item[QStringLiteral("col")] = tile.col;
        item[QStringLiteral("letter")] = QString(QChar(tile.letter));
        item[QStringLiteral("blank")] = tile.blank;
        lastPlay.append(item);
    }

    QJsonObject data;
    data[QStringLiteral("version")] = 1;
    data[QStringLiteral("board")] = QString::fromStdString(boardText);
    data[QStringLiteral("racks")] = racks;
    data[QStringLiteral("bag")] = QString::fromStdString(std::string(bag_.begin(), bag_.end()));
    data[QStringLiteral("scores")] = QJsonArray{scores_[0], scores_[1]};
    data[QStringLiteral("turn")] = turn_;
    data[QStringLiteral("scoreless")] = scoreless_;
    data[QStringLiteral("finished")] = finished_;
    data[QStringLiteral("rngState")] = static_cast<qint64>(rngState_);
    data[QStringLiteral("log")] = log;
    data[QStringLiteral("lastPlay")] = lastPlay;
    return data;
}

std::optional<Game> Game::fromJson(const QJsonObject &data) {
    if (intField(data, "version", 1, 1) != 1)
        return std::nullopt;

    const QJsonValue boardValue = data.value(QLatin1StringView("board"));
    if (!boardValue.isString())
        return std::nullopt;
    const std::string boardText = boardValue.toString().toStdString();
    if (boardText.size() != static_cast<std::size_t>(kSquares))
        return std::nullopt;

    Game game;
    std::array<int, kTileKinds> counts{};
    for (int i = 0; i < kSquares; ++i) {
        const char symbol = boardText[i];
        if (symbol == '.')
            continue;
        if (symbol >= 'a' && symbol <= 'z') {
            game.board_[i] = Cell{static_cast<char>(symbol - 'a' + 'A'), true};
            ++counts[kBlankIndex];
        } else if (symbol >= 'A' && symbol <= 'Z') {
            game.board_[i] = Cell{symbol, false};
            ++counts[tileIndex(symbol)];
        } else {
            return std::nullopt;
        }
    }

    const auto racks = arrayField(data, "racks", 2);
    if (!racks || racks->size() != 2)
        return std::nullopt;
    for (int player = 0; player < 2; ++player) {
        const auto rack = tilesOf(racks->at(player), kRackSize);
        if (!rack)
            return std::nullopt;
        game.racks_[player] = *rack;
    }

    const auto bag = tilesOf(data.value(QLatin1StringView("bag")), kTileTotal);
    if (!bag)
        return std::nullopt;
    game.bag_ = *bag;

    for (const auto &rack : game.racks_)
        for (char tile : rack)
            ++counts[tileIndex(tile)];
    for (char tile : game.bag_)
        ++counts[tileIndex(tile)];
    for (int kind = 0; kind < kTileKinds; ++kind) {
        const char tile = kind == kBlankIndex ? '?' : static_cast<char>('A' + kind);
        if (counts[kind] != tileCount(tile))
            return std::nullopt;
    }

    const auto scores = arrayField(data, "scores", 2);
    if (!scores || scores->size() != 2)
        return std::nullopt;
    for (int player = 0; player < 2; ++player) {
        const auto score = integerValue(scores->at(player), -kMaxScore, kMaxScore);
        if (!score)
            return std::nullopt;
        game.scores_[player] = static_cast<int>(*score);
    }

    const auto turn = intField(data, "turn", 0, 1);
    const auto scoreless = intField(data, "scoreless", 0, kScorelessLimit);
    const QJsonValue finished = data.value(QLatin1StringView("finished"));
    const auto rngState = integerValue(data.value(QLatin1StringView("rngState")), 1, 0xFFFFFFFFLL);
    if (!turn || !scoreless || !finished.isBool() || !rngState)
        return std::nullopt;
    game.turn_ = *turn;
    game.scoreless_ = *scoreless;
    game.finished_ = finished.toBool();
    game.rngState_ = static_cast<std::uint32_t>(*rngState);

    const bool racksFull = game.racks_[0].size() == kRackSize && game.racks_[1].size() == kRackSize;
    const bool anyEmpty = game.racks_[0].empty() || game.racks_[1].empty();
    if (!game.bag_.empty() && !racksFull)
        return std::nullopt;
    if (game.bag_.empty() && !game.finished_ && anyEmpty)
        return std::nullopt;
    if (game.scoreless_ >= kScorelessLimit && !game.finished_)
        return std::nullopt;

    const auto log = arrayField(data, "log", kMaxLogEntries);
    if (!log)
        return std::nullopt;
    for (const QJsonValue &element : *log) {
        if (!element.isObject())
            return std::nullopt;
        const QJsonObject entry = element.toObject();
        const auto player = intField(entry, "player", 0, 1);
        const auto score = intField(entry, "score", -kMaxScore, kMaxScore);
        const QJsonValue kind = entry.value(QLatin1StringView("kind"));
        const QJsonValue text = entry.value(QLatin1StringView("text"));
        if (!player || !score || !kind.isString() || !text.isString())
            return std::nullopt;
        const std::string kindText = kind.toString().toStdString();
        const std::string body = text.toString().toStdString();
        LogEntry logEntry;
        logEntry.player = *player;
        logEntry.score = *score;
        logEntry.text = body;
        if (kindText == "play") {
            logEntry.kind = TurnKind::Play;
            if (body.size() > static_cast<std::size_t>(kBoardSize))
                return std::nullopt;
            for (char letter : body)
                if (letter < 'A' || letter > 'Z')
                    return std::nullopt;
        } else if (kindText == "pass") {
            logEntry.kind = TurnKind::Pass;
            if (!body.empty())
                return std::nullopt;
        } else if (kindText == "exchange") {
            logEntry.kind = TurnKind::Exchange;
            if (body.empty() || body.size() > 3)
                return std::nullopt;
            for (char digit : body)
                if (digit < '0' || digit > '9')
                    return std::nullopt;
        } else {
            return std::nullopt;
        }
        game.log_.push_back(std::move(logEntry));
    }

    const auto lastPlay = arrayField(data, "lastPlay", kMaxLastPlay);
    if (!lastPlay)
        return std::nullopt;
    for (const QJsonValue &element : *lastPlay) {
        if (!element.isObject())
            return std::nullopt;
        const QJsonObject item = element.toObject();
        const auto row = intField(item, "row", 0, kBoardSize - 1);
        const auto col = intField(item, "col", 0, kBoardSize - 1);
        const QJsonValue letter = item.value(QLatin1StringView("letter"));
        const QJsonValue blank = item.value(QLatin1StringView("blank"));
        if (!row || !col || !letter.isString() || !blank.isBool())
            return std::nullopt;
        const std::string letterText = letter.toString().toStdString();
        if (letterText.size() != 1 || letterText[0] < 'A' || letterText[0] > 'Z')
            return std::nullopt;
        const Cell &cell = cellAt(game.board_, *row, *col);
        if (cell.empty() || cell.letter != letterText[0] || cell.blank != blank.toBool())
            return std::nullopt;
        game.lastPlay_.push_back(Placement{*row, *col, letterText[0], blank.toBool()});
    }

    return game;
}

}
