/**
 * @file window.cpp
 * @brief Main window: layout, live theming, input handling, and the turn flow.
 *
 * The window background carries the theme's opacity; every other surface is solid. Theme
 * updates restyle the existing widgets in place, so the game state is never disturbed.
 * All popups are Modal layers inside this window.
 */
#include "window.h"
#include "board_view.h"
#include "icons.h"
#include "modals.h"
#include "rack_view.h"

#include "core/storage.h"

#include <QCloseEvent>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QRandomGenerator>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <numeric>
#include <utility>

namespace scrabble {
namespace {

constexpr int kSideWidth = 230;  ///< Width of the move list and of its balancing twin.
constexpr int kGap = 14;         ///< Space between the board and each side panel.
constexpr int kMargins = 36;     ///< Left and right window margins together.

QString mixed(const QColor &base, const QColor &tint, double amount) {
    return QColor::fromRgbF(base.redF() * (1 - amount) + tint.redF() * amount,
                            base.greenF() * (1 - amount) + tint.greenF() * amount,
                            base.blueF() * (1 - amount) + tint.blueF() * amount).name();
}

int levelIndex(Level level) {
    return static_cast<int>(level);
}

QIcon themedIcon(Icon icon, const QColor &normal, const QColor &disabled, qreal ratio) {
    QIcon result = makeIcon(icon, normal, 22, ratio);
    result.addPixmap(makeIcon(icon, disabled, 22, ratio).pixmap(22), QIcon::Disabled);
    return result;
}

QToolButton *actionButton(const QString &text, const QString &name) {
    auto *button = new QToolButton;
    button->setObjectName(name);
    button->setText(text);
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setIconSize({22, 22});
    button->setCursor(Qt::PointingHandCursor);
    button->setAccessibleName(text);
    button->setFocusPolicy(Qt::NoFocus); // every action has a key; Tab shuffles instead
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setMinimumWidth(58);
    return button;
}

QFrame *scoreCard(const QString &name, QLabel *&score, const QString &object) {
    auto *card = new QFrame;
    card->setObjectName("scoreCard");
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 8, 14, 8);
    layout->setSpacing(0);
    auto *label = new QLabel(name);
    label->setObjectName("scoreName");
    score = new QLabel("0");
    score->setObjectName(object);
    layout->addWidget(label);
    layout->addWidget(score);
    return card;
}

}

Window::Window(const QString &configDirectory, const Theme &theme, std::optional<QString> level,
               std::optional<std::uint32_t> seed, QWidget *parent)
    : QWidget(parent), gamePath_(configDirectory + "/game.json"), statsPath_(configDirectory + "/stats.json"),
      theme_(theme), watcher_(configDirectory + "/theme.json", theme),
      random_(QRandomGenerator::global()->generate()) {
    setWindowTitle("Scrabble");
    setAttribute(Qt::WA_TranslucentBackground);
    setMinimumSize(620, 760);
    resize(900, 900);
    statistics_ = Statistics::load(statsPath_);
    buildUi();
    connect(&watcher_, &ThemeWatcher::changed, this, &Window::themeChanged);
    connect(&watcher_, &ThemeWatcher::rejected, this, [this](const QString &message) {
        themeErrorShown_ = true;
        setStatus("Theme not applied: " + message, true);
    });
    connect(&watcher_, &ThemeWatcher::recovered, this, [this] {
        if (themeErrorShown_) {
            themeErrorShown_ = false;
            setStatus("");
            refresh();
        }
    });
    computerTimer_.setSingleShot(true);
    connect(&computerTimer_, &QTimer::timeout, this, &Window::computerMove);

    // A level given on the command line applies to a new game and takes over a resumed one.
    if (level)
        level_ = parseLevel(level->toStdString());
    if (seed || !loadSavedGame())
        startGame(seed);
    if (level) {
        level_ = parseLevel(level->toStdString());
        hintBank_ = HintBank::restore(hintBank_.available(), std::min(hintBank_.progress(), HintBank::wordsPerHint(level_) - 1), level_)
                        .value_or(HintBank(level_));
        saveGame();
    }
    applyTheme();
    refresh();
    if (game_->turn() == Game::kComputer && !game_->finished())
        startComputerTurn();
}

Window::~Window() = default;

void Window::buildUi() {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    content_ = new QWidget;
    outer->addWidget(content_);

    auto *root = new QVBoxLayout(content_);
    root->setContentsMargins(18, 16, 18, 16);
    root->setSpacing(12);

    auto *header = new QHBoxLayout;
    header->setSpacing(12);
    youCard_ = scoreCard("You", youScore_, "scoreValue");
    computerCard_ = scoreCard("Computer", computerScore_, "scoreValue");
    auto *middle = new QVBoxLayout;
    middle->setSpacing(2);
    bag_ = new QLabel;
    bag_->setObjectName("bagLabel");
    bag_->setAlignment(Qt::AlignCenter);
    statusLabel_ = new QLabel;
    statusLabel_->setObjectName("statusLabel");
    statusLabel_->setAlignment(Qt::AlignCenter);
    statusLabel_->setWordWrap(true);
    statusLabel_->setMinimumHeight(40);
    middle->addWidget(bag_);
    middle->addWidget(statusLabel_);
    header->addWidget(youCard_, 1);
    header->addLayout(middle, 3);
    header->addWidget(computerCard_, 1);
    root->addLayout(header);

    auto *body = new QHBoxLayout;
    body->setSpacing(14);
    // An empty twin of the move list on the left keeps the board centered in the window.
    balance_ = new QWidget;
    balance_->setFixedWidth(kSideWidth);
    body->addWidget(balance_);
    board_ = new BoardView;
    board_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    body->addWidget(board_, 1);
    side_ = new QFrame;
    side_->setObjectName("sidePanel");
    side_->setFixedWidth(kSideWidth);
    auto *sideLayout = new QVBoxLayout(side_);
    sideLayout->setContentsMargins(14, 12, 14, 12);
    auto *sideTitle = new QLabel("Moves");
    sideTitle->setObjectName("scoreName");
    history_ = new QLabel;
    history_->setObjectName("history");
    history_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    history_->setWordWrap(true);
    sideLayout->addWidget(sideTitle);
    sideLayout->addWidget(history_, 1);
    body->addWidget(side_);
    root->addLayout(body, 1);

    rack_ = new RackView;
    rack_->setFixedHeight(64);
    root->addWidget(rack_);

    auto *actions = new QHBoxLayout;
    actions->setSpacing(6);
    play_ = actionButton("Play", "playButton");
    recall_ = actionButton("Recall", "actionButton");
    shuffle_ = actionButton("Shuffle", "actionButton");
    exchange_ = actionButton("Exchange", "actionButton");
    pass_ = actionButton("Pass", "actionButton");
    hint_ = actionButton("Hint", "actionButton");
    newGame_ = actionButton("New", "actionButton");
    stats_ = actionButton("Stats", "actionButton");
    rules_ = actionButton("Rules", "actionButton");
    for (auto *button : {play_, recall_, shuffle_, exchange_, pass_, hint_, newGame_, stats_, rules_})
        actions->addWidget(button);
    root->addLayout(actions);

    modal_ = new Modal(this, content_);
    modal_->setGeometry(rect());

    connect(board_, &BoardView::squareClicked, this, &Window::clickSquare);
    connect(board_, &BoardView::pendingClicked, this, [this](int row, int col) {
        if (!humanTurn())
            return;
        std::erase_if(pending_, [&](const PendingTile &tile) { return tile.placement.row == row && tile.placement.col == col; });
        selectedRack_ = -1;
        refresh();
    });
    connect(rack_, &RackView::tileClicked, this, &Window::selectRackSlot);
    connect(play_, &QToolButton::clicked, this, &Window::submitPlay);
    connect(recall_, &QToolButton::clicked, this, &Window::recallAll);
    connect(shuffle_, &QToolButton::clicked, this, &Window::doShuffle);
    connect(exchange_, &QToolButton::clicked, this, &Window::askExchange);
    connect(pass_, &QToolButton::clicked, this, &Window::askPass);
    connect(hint_, &QToolButton::clicked, this, &Window::doHint);
    connect(newGame_, &QToolButton::clicked, this, &Window::askNewGame);
    connect(stats_, &QToolButton::clicked, this, [this] { modal_->showStats(statistics_); });
    connect(rules_, &QToolButton::clicked, this, [this] { modal_->showRules(); });

    connect(modal_, &Modal::letterChosen, this, [this](char letter) {
        if (ask_ == Ask::Blank && awaitingBlank_) {
            const auto tile = *awaitingBlank_;
            awaitingBlank_.reset();
            ask_ = Ask::None;
            placeTile(tile.rackIndex, tile.placement.row, tile.placement.col, letter);
        }
        setFocus();
    });
    connect(modal_, &Modal::answered, this, [this](int index) {
        const Ask ask = std::exchange(ask_, Ask::None);
        if (ask == Ask::Pass && index == 1 && humanTurn() && game_->pass()) {
            setStatus("You passed.");
            afterTurn();
        } else if (ask == Ask::GameOver && index == 1) {
            askNewGame();
            return;
        }
        refresh();
        setFocus();
    });
    connect(modal_, &Modal::exchangeChosen, this, [this](const std::vector<char> &tiles) {
        ask_ = Ask::None;
        if (!tiles.empty() && humanTurn() && game_->exchange(tiles)) {
            setStatus(QString("You exchanged %1 tile%2.").arg(tiles.size()).arg(tiles.size() == 1 ? "" : "s"));
            afterTurn();
        }
        refresh();
        setFocus();
    });
    connect(modal_, &Modal::newGameChosen, this, [this](int level) {
        ask_ = Ask::None;
        level_ = static_cast<Level>(std::clamp(level, 0, 2));
        startGame(std::nullopt);
        refresh();
        setFocus();
    });
    connect(modal_, &Modal::dismissed, this, [this] {
        const Ask ask = std::exchange(ask_, Ask::None);
        awaitingBlank_.reset();
        if (ask == Ask::NewGame && !game_->finished() && game_->turn() == Game::kComputer)
            startComputerTurn();
        refresh();
        setFocus();
    });
    setFocusPolicy(Qt::StrongFocus);
    setFocus();
}

void Window::applyTheme() {
    const auto color = [this](const char *key) { return theme_.color(key); };
    const QString text = theme_.hex("text"), muted = theme_.hex("muted"), surface = theme_.hex("surface"),
        alt = theme_.hex("surface_alt"), border = theme_.hex("border"), accent = theme_.hex("accent"),
        accentText = theme_.hex("accent_text"), danger = theme_.hex("danger"),
        hover = mixed(color("surface_alt"), color("accent"), .18);
    const QString style = QString(R"(
        QWidget { color: %1; }
        QFrame#scoreCard, QFrame#sidePanel { background: %3; border: 1px solid %5; border-radius: 12px; }
        QFrame#scoreCard[active="true"] { border: 2px solid %6; }
        QLabel { background: transparent; }
        QLabel#scoreName { color: %2; font-size: 12px; font-weight: 600; letter-spacing: 1px; }
        QLabel#scoreValue { font-size: 28px; font-weight: 700; }
        QLabel#bagLabel { color: %2; font-size: 12px; font-weight: 600; letter-spacing: 1px; }
        QLabel#statusLabel { font-size: 14px; }
        QLabel#statusLabel[error="true"] { color: %9; }
        QLabel#history { color: %2; font-size: 13px; }
        QToolButton { background: %3; color: %1; border: 1px solid %5; border-radius: 10px; padding: 6px 2px 4px 2px; font-size: 11px; }
        QToolButton:hover { background: %10; }
        QToolButton:focus { border: 2px solid %6; }
        QToolButton:disabled { color: %2; background: %4; border-color: %4; }
        QToolButton#playButton { background: %6; color: %7; border-color: %6; font-weight: 700; }
        QToolButton#playButton:hover { background: %8; border-color: %8; }
        QToolButton#playButton:disabled { background: %4; color: %2; border-color: %4; }
        QFrame#modalCard { background: %3; border: 1px solid %5; border-radius: 16px; }
        QLabel#modalTitle { font-size: 20px; font-weight: 700; }
        QLabel#modalBody { color: %2; font-size: 13px; }
        QLabel#modalColumn { color: %2; font-size: 12px; font-weight: 600; }
        QLabel#modalValue { font-size: 14px; font-weight: 600; }
        QPushButton { background: %4; color: %1; border: 1px solid %5; border-radius: 9px; padding: 8px 18px; font-size: 13px; }
        QPushButton:hover { background: %10; }
        QPushButton:focus { border: 2px solid %6; }
        QPushButton:disabled { color: %2; background: %4; border-color: %4; }
        QPushButton[primary="true"] { background: %6; color: %7; border-color: %6; font-weight: 700; }
        QPushButton[primary="true"]:hover { background: %8; border-color: %8; }
        QPushButton[primary="true"]:disabled { background: %4; color: %2; border-color: %4; }
        QPushButton[choice="true"]:checked { background: %6; color: %7; border-color: %6; font-weight: 700; }
    )").arg(text, muted, surface, alt, border, accent, accentText, mixed(color("accent"), color("text"), .2), danger, hover);
    setStyleSheet(style);

    QPalette palette;
    palette.setColor(QPalette::Window, color("background"));
    palette.setColor(QPalette::WindowText, color("text"));
    palette.setColor(QPalette::Base, color("surface"));
    palette.setColor(QPalette::Text, color("text"));
    palette.setColor(QPalette::Button, color("surface"));
    palette.setColor(QPalette::ButtonText, color("text"));
    palette.setColor(QPalette::Highlight, color("accent"));
    palette.setColor(QPalette::HighlightedText, color("accent_text"));
    setPalette(palette);

    const qreal ratio = devicePixelRatioF();
    const QColor normal = color("text"), disabled = color("muted"), onAccent = color("accent_text");
    play_->setIcon(themedIcon(Icon::Play, onAccent, disabled, ratio));
    recall_->setIcon(themedIcon(Icon::Recall, normal, disabled, ratio));
    shuffle_->setIcon(themedIcon(Icon::Shuffle, normal, disabled, ratio));
    exchange_->setIcon(themedIcon(Icon::Exchange, normal, disabled, ratio));
    pass_->setIcon(themedIcon(Icon::Pass, normal, disabled, ratio));
    hint_->setIcon(themedIcon(Icon::Hint, normal, disabled, ratio));
    newGame_->setIcon(themedIcon(Icon::NewGame, normal, disabled, ratio));
    stats_->setIcon(themedIcon(Icon::Stats, normal, disabled, ratio));
    rules_->setIcon(themedIcon(Icon::Rules, normal, disabled, ratio));

    board_->setTheme(theme_);
    rack_->setTheme(theme_);
    modal_->setTheme(theme_);
    update();
}

void Window::themeChanged() {
    theme_ = watcher_.theme();
    applyTheme();
    refresh();
}

void Window::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), theme_.backgroundWithAlpha());
}

void Window::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    modal_->setGeometry(rect());
    if (!panelsQueued_) {
        panelsQueued_ = true;
        QMetaObject::invokeMethod(this, &Window::updatePanels, Qt::QueuedConnection);
    }
}

bool Window::focusNextPrevChild(bool next) {
    // Tab is the shuffle key; the modal keeps its own focus cycle.
    if (modal_->isVisible())
        return QWidget::focusNextPrevChild(next);
    doShuffle();
    return true;
}

void Window::closeEvent(QCloseEvent *event) {
    saveGame();
    QWidget::closeEvent(event);
}

void Window::updatePanels() {
    panelsQueued_ = false;
    // The panels appear only when the board keeps its full height; otherwise they would shrink it.
    // The board's height does not depend on the panels, so this cannot flip back and forth. It is read
    // after the layout has settled, because the first resize reports the pre-layout size.
    layout()->activate();
    const bool room = width() - kMargins - 2 * (kSideWidth + kGap) >= board_->height();
    side_->setVisible(room);
    balance_->setVisible(room);
}

bool Window::humanTurn() const {
    return game_ && !game_->finished() && game_->turn() == Game::kHuman && !busy_ && !modal_->isVisible();
}

void Window::startGame(std::optional<std::uint32_t> seed) {
    computerTimer_.stop();
    busy_ = false;
    pending_.clear();
    awaitingBlank_.reset();
    selectedRack_ = -1;
    cursorRow_ = cursorCol_ = 7;
    horizontal_ = true;
    statsRecorded_ = false;
    bestPlay_ = 0;
    hintsUsed_ = 0;
    hintBank_ = HintBank(level_);
    hintShown_.clear();
    game_ = std::make_unique<Game>(seed ? *seed : QRandomGenerator::global()->generate());
    order_.assign(game_->rack(Game::kHuman).size(), 0);
    std::iota(order_.begin(), order_.end(), 0);
    setStatus("Your turn. Cover the center star to start.");
    saveGame();
}

bool Window::loadSavedGame() {
    try {
        const auto data = readJson(gamePath_);
        if (data.value("version").toInt() != 1)
            return false;
        auto restored = Game::fromJson(data.value("game").toObject());
        if (!restored || restored->finished())
            return false;
        game_ = std::make_unique<Game>(std::move(*restored));
        level_ = parseLevel(data.value("level").toString().toStdString());
        bestPlay_ = std::max(0, data.value("best_play").toInt());
        hintsUsed_ = std::max(0, data.value("hints").toInt());
        // Games saved before hints were limited start with the level's allowance.
        hintBank_ = HintBank::restore(data.value("hints_left").toInt(-1), data.value("hint_progress").toInt(0), level_)
                        .value_or(HintBank(level_));
        hintShown_.clear();
        statsRecorded_ = data.value("stats_recorded").toBool();
        order_.assign(game_->rack(Game::kHuman).size(), 0);
        std::iota(order_.begin(), order_.end(), 0);
        setStatus(game_->turn() == Game::kHuman ? "Welcome back. Your turn." : "Welcome back.");
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

void Window::saveGame() {
    if (!game_)
        return;
    try {
        writeJson(gamePath_, {{"version", 1}, {"level", levelName(level_)}, {"best_play", bestPlay_},
                              {"hints", hintsUsed_}, {"hints_left", hintBank_.available()},
                              {"hint_progress", hintBank_.progress()}, {"stats_recorded", statsRecorded_},
                              {"game", game_->toJson()}},
                  QJsonDocument::Compact);
    } catch (const std::exception &) {
        setStatus("The game could not be saved.", true);
    }
}

void Window::setStatus(const QString &text, bool error) {
    status_ = text;
    statusIsError_ = error;
    if (statusLabel_) {
        statusLabel_->setText(text);
        statusLabel_->setProperty("error", error);
        statusLabel_->style()->unpolish(statusLabel_);
        statusLabel_->style()->polish(statusLabel_);
    }
}

QString Window::describePlay(const LogEntry &entry) const {
    const QString who = entry.player == Game::kHuman ? "You" : "CPU";
    switch (entry.kind) {
    case TurnKind::Play: return QString("%1  %2  +%3").arg(who, QString::fromStdString(entry.text)).arg(entry.score);
    case TurnKind::Pass: return QString("%1  passed").arg(who);
    case TurnKind::Exchange: return QString("%1  exchanged %2").arg(who, QString::fromStdString(entry.text));
    }
    return {};
}

std::vector<Placement> Window::pendingPlacements() const {
    std::vector<Placement> result;
    for (const auto &tile : pending_)
        result.push_back(tile.placement);
    return result;
}

std::vector<bool> Window::usedFlags() const {
    std::vector<bool> used(game_->rack(Game::kHuman).size(), false);
    for (const auto &tile : pending_)
        used[tile.rackIndex] = true;
    return used;
}

bool Window::pendingAt(int row, int col) const {
    return std::any_of(pending_.begin(), pending_.end(), [&](const PendingTile &tile) {
        return tile.placement.row == row && tile.placement.col == col;
    });
}

void Window::refresh() {
    const auto &rack = game_->rack(Game::kHuman);
    if (order_.size() != rack.size()) {
        order_.assign(rack.size(), 0);
        std::iota(order_.begin(), order_.end(), 0);
    }
    const auto used = usedFlags();
    std::vector<char> tiles;
    std::vector<bool> usedInOrder;
    int selectedSlot = -1;
    for (int slot = 0; slot < static_cast<int>(order_.size()); ++slot) {
        tiles.push_back(rack[order_[slot]]);
        usedInOrder.push_back(used[order_[slot]]);
        if (order_[slot] == selectedRack_)
            selectedSlot = slot;
    }
    rack_->setTiles(tiles, usedInOrder);
    rack_->setSelected(selectedSlot);
    board_->setBoard(game_->board());
    board_->setPending(pendingPlacements());
    board_->setLastPlay(game_->lastPlay());
    board_->setKeyboardCursor(humanTurn() && selectedRack_ < 0 ? cursorRow_ : -1, cursorCol_, horizontal_);

    youScore_->setText(QString::number(game_->score(Game::kHuman)));
    computerScore_->setText(QString::number(game_->score(Game::kComputer)));
    youCard_->setProperty("active", !game_->finished() && game_->turn() == Game::kHuman);
    computerCard_->setProperty("active", !game_->finished() && game_->turn() == Game::kComputer);
    for (auto *card : {youCard_, computerCard_}) {
        card->style()->unpolish(card);
        card->style()->polish(card);
    }
    bag_->setText(QString("BAG %1  ·  %2  ·  HINTS %3  ·  NEXT IN %4")
                      .arg(game_->bagSize()).arg(QString(levelName(level_)).toUpper())
                      .arg(hintBank_.available()).arg(hintBank_.wordsUntilNext(level_)));
    hint_->setText(QString("Hint (%1)").arg(hintBank_.available()));
    statusLabel_->setText(status_);

    QStringList lines;
    const auto &log = game_->log();
    for (int index = static_cast<int>(log.size()) - 1; index >= 0 && lines.size() < 14; --index)
        lines << describePlay(log[index]);
    history_->setText(lines.isEmpty() ? "No moves yet." : lines.join('\n'));

    const bool active = humanTurn();
    const bool anyPending = !pending_.empty();
    play_->setEnabled(active && anyPending);
    recall_->setEnabled(active && anyPending);
    shuffle_->setEnabled(active);
    exchange_->setEnabled(active && game_->canExchange());
    pass_->setEnabled(active);
    hint_->setEnabled(active);
}

void Window::selectRackSlot(int slot) {
    if (!humanTurn() || slot < 0 || slot >= static_cast<int>(order_.size()))
        return;
    const int index = order_[slot];
    selectedRack_ = selectedRack_ == index ? -1 : index;
    refresh();
}

void Window::clickSquare(int row, int col) {
    if (!humanTurn())
        return;
    if (!cellAt(game_->board(), row, col).empty())
        return;
    if (selectedRack_ >= 0) {
        placeTile(selectedRack_, row, col, 0);
        return;
    }
    // Without a selected tile, a click moves the cursor; clicking it again flips direction.
    if (row == cursorRow_ && col == cursorCol_)
        horizontal_ = !horizontal_;
    cursorRow_ = row;
    cursorCol_ = col;
    refresh();
}

void Window::placeTile(int rackIndex, int row, int col, char blankLetter) {
    const auto &rack = game_->rack(Game::kHuman);
    if (rackIndex < 0 || rackIndex >= static_cast<int>(rack.size()) || pendingAt(row, col)
        || !cellAt(game_->board(), row, col).empty())
        return;
    const auto used = usedFlags();
    if (used[rackIndex])
        return;
    const char tile = rack[rackIndex];
    if (tile == '?' && !blankLetter) {
        awaitingBlank_ = PendingTile{{row, col, 0, true}, rackIndex};
        ask_ = Ask::Blank;
        modal_->showLetterPicker();
        return;
    }
    pending_.push_back({{row, col, tile == '?' ? blankLetter : tile, tile == '?'}, rackIndex});
    selectedRack_ = -1;
    cursorRow_ = row;
    cursorCol_ = col;
    advanceCursor();
    setStatus("");
    refresh();
}

void Window::advanceCursor() {
    int row = cursorRow_, col = cursorCol_;
    for (int step = 0; step < kBoardSize; ++step) {
        row += horizontal_ ? 0 : 1;
        col += horizontal_ ? 1 : 0;
        if (row >= kBoardSize || col >= kBoardSize)
            return;
        if (cellAt(game_->board(), row, col).empty() && !pendingAt(row, col)) {
            cursorRow_ = row;
            cursorCol_ = col;
            return;
        }
    }
}

void Window::moveCursor(int dRow, int dCol) {
    cursorRow_ = std::clamp(cursorRow_ + dRow, 0, kBoardSize - 1);
    cursorCol_ = std::clamp(cursorCol_ + dCol, 0, kBoardSize - 1);
    refresh();
}

void Window::typeLetter(char letter) {
    if (!cellAt(game_->board(), cursorRow_, cursorCol_).empty() || pendingAt(cursorRow_, cursorCol_)) {
        setStatus("Pick an empty square first.", true);
        refresh();
        return;
    }
    const auto &rack = game_->rack(Game::kHuman);
    const auto used = usedFlags();
    int blankIndex = -1;
    for (int index = 0; index < static_cast<int>(rack.size()); ++index) {
        if (used[index])
            continue;
        if (rack[index] == letter) {
            placeTile(index, cursorRow_, cursorCol_, 0);
            return;
        }
        if (rack[index] == '?' && blankIndex < 0)
            blankIndex = index;
    }
    if (blankIndex >= 0)
        placeTile(blankIndex, cursorRow_, cursorCol_, letter);
    else {
        setStatus(QString("You have no %1 tile.").arg(QChar(letter)), true);
        refresh();
    }
}

void Window::recallLast() {
    if (pending_.empty())
        return;
    cursorRow_ = pending_.back().placement.row;
    cursorCol_ = pending_.back().placement.col;
    pending_.pop_back();
    refresh();
}

void Window::recallAll() {
    pending_.clear();
    selectedRack_ = -1;
    setStatus("");
    refresh();
}

void Window::doShuffle() {
    if (!humanTurn())
        return;
    for (int index = static_cast<int>(order_.size()) - 1; index > 0; --index)
        std::swap(order_[index], order_[random_() % (index + 1)]);
    refresh();
}

void Window::keyPressEvent(QKeyEvent *event) {
    const auto modifiers = event->modifiers() & ~Qt::KeypadModifier;
    const int key = event->key();
    if (modifiers == Qt::ControlModifier) {
        if (key == Qt::Key_N)
            askNewGame();
        else if (key == Qt::Key_E && humanTurn() && game_->canExchange())
            askExchange();
        else if (key == Qt::Key_P && humanTurn())
            askPass();
        else if (key == Qt::Key_H && humanTurn())
            doHint();
        else if (key == Qt::Key_I)
            modal_->showStats(statistics_);
        else
            QWidget::keyPressEvent(event);
        return;
    }
    if (key == Qt::Key_F1) {
        modal_->showRules();
        return;
    }
    if (!humanTurn()) {
        QWidget::keyPressEvent(event);
        return;
    }
    if (key >= Qt::Key_A && key <= Qt::Key_Z && !(modifiers & (Qt::AltModifier | Qt::MetaModifier))) {
        typeLetter(char('A' + key - Qt::Key_A));
    } else if (key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down) {
        moveCursor(key == Qt::Key_Up ? -1 : key == Qt::Key_Down ? 1 : 0, key == Qt::Key_Left ? -1 : key == Qt::Key_Right ? 1 : 0);
    } else if (key == Qt::Key_Space) {
        horizontal_ = !horizontal_;
        refresh();
    } else if (key == Qt::Key_Backspace) {
        recallLast();
    } else if (key == Qt::Key_Escape || key == Qt::Key_Delete) {
        recallAll();
    } else if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        submitPlay();
    } else {
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
}

void Window::submitPlay() {
    if (!humanTurn() || pending_.empty())
        return;
    const auto evaluation = evaluatePlacement(game_->board(), pendingPlacements(), Dawg::english());
    if (!evaluation.valid) {
        setStatus(QString::fromStdString(evaluation.error), true);
        refresh();
        return;
    }
    if (!game_->play(evaluation.move)) {
        setStatus("That play is not possible.", true);
        refresh();
        return;
    }
    bestPlay_ = std::max(bestPlay_, evaluation.move.score);
    QString message = QString("You played %1 for %2.").arg(QString::fromStdString(evaluation.move.words.front())).arg(evaluation.move.score);
    hintEarned_ = hintBank_.recordWord(level_);
    if (hintEarned_)
        message += " You earned a hint!";
    setStatus(message);
    hintShown_.clear();
    pending_.clear();
    selectedRack_ = -1;
    afterTurn();
}

void Window::doHint() {
    if (!humanTurn())
        return;
    // Pressing Hint again while its preview is still on the board does not cost another hint.
    const auto shown = pendingPlacements();
    if (!hintShown_.empty() && shown.size() == hintShown_.size()
        && std::is_permutation(shown.begin(), shown.end(), hintShown_.begin())) {
        setStatus("That hint is on the board. Press Play to use it.");
        refresh();
        return;
    }
    if (hintBank_.available() <= 0) {
        const int needed = hintBank_.wordsUntilNext(level_);
        setStatus(QString("No hints left. Play %1 more word%2 to earn one.").arg(needed).arg(needed == 1 ? "" : "s"), true);
        refresh();
        return;
    }
    recallAll();
    const auto best = chooseMove(game_->board(), game_->rack(Game::kHuman), Dawg::english(), Level::Hard, random_);
    if (!best) {
        setStatus("No play is available. Exchange tiles or pass.", true);
        refresh();
        return;
    }
    const auto &rack = game_->rack(Game::kHuman);
    std::vector<bool> taken(rack.size(), false);
    for (const auto &placement : best->tiles) {
        for (int index = 0; index < static_cast<int>(rack.size()); ++index) {
            if (!taken[index] && rack[index] == (placement.blank ? '?' : placement.letter)) {
                taken[index] = true;
                pending_.push_back({placement, index});
                break;
            }
        }
    }
    hintBank_.spend();
    ++hintsUsed_;
    hintShown_ = pendingPlacements();
    setStatus(QString("Hint: %1 for %2. Press Play to use it.").arg(QString::fromStdString(best->words.front())).arg(best->score));
    saveGame();
    refresh();
}

void Window::askPass() {
    if (!humanTurn())
        return;
    recallAll();
    ask_ = Ask::Pass;
    modal_->showMessage("Pass this turn?", "You score nothing and the computer moves next.", {"Keep playing", "Pass"}, 0);
}

void Window::askExchange() {
    if (!humanTurn() || !game_->canExchange())
        return;
    recallAll();
    ask_ = Ask::Exchange;
    modal_->showExchange(game_->rack(Game::kHuman));
}

void Window::askNewGame() {
    computerTimer_.stop();
    ask_ = Ask::NewGame;
    modal_->showNewGame(levelIndex(level_), !game_->finished() && !game_->log().empty());
}

void Window::afterTurn() {
    order_.clear();
    refresh();
    saveGame();
    finishIfOver();
    if (!game_->finished() && game_->turn() == Game::kComputer)
        startComputerTurn();
}

void Window::startComputerTurn() {
    busy_ = true;
    setStatus(hintEarned_ ? "You earned a hint! The computer is thinking…" : "The computer is thinking…");
    refresh();
    computerTimer_.start(700);
}

void Window::computerMove() {
    busy_ = false;
    if (game_->finished() || game_->turn() != Game::kComputer)
        return;
    const auto &rack = game_->rack(Game::kComputer);
    const auto move = chooseMove(game_->board(), rack, Dawg::english(), level_, random_);
    if (move && game_->play(*move)) {
        setStatus(QString("Computer played %1 for %2.").arg(QString::fromStdString(move->words.front())).arg(move->score));
    } else if (game_->canExchange() && game_->exchange(chooseExchange(rack, level_))) {
        setStatus("The computer exchanged tiles.");
    } else {
        game_->pass();
        setStatus("The computer passed.");
    }
    if (hintEarned_)
        setStatus(status_ + " You earned a hint!");
    hintEarned_ = false;
    afterTurn();
}

void Window::finishIfOver() {
    if (!game_->finished() || statsRecorded_)
        return;
    statsRecorded_ = true;
    statistics_.recordGame(levelIndex(level_), game_->score(Game::kHuman), game_->score(Game::kComputer), bestPlay_);
    try {
        statistics_.save(statsPath_);
    } catch (const std::exception &) {
    }
    saveGame();
    const int winner = game_->winner();
    const QString title = winner == Game::kHuman ? "You win!" : winner == Game::kComputer ? "The computer wins" : "It's a tie";
    const QString body = QString("Final score: You %1 – Computer %2").arg(game_->score(Game::kHuman)).arg(game_->score(Game::kComputer));
    setStatus(title);
    refresh();
    ask_ = Ask::GameOver;
    modal_->showMessage(title, body, {"Close", "New game"}, 1);
}

}
