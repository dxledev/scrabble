#pragma once

#include "core/ai.h"
#include "core/game.h"
#include "core/statistics.h"
#include "core/theme.h"
#include "theme_watcher.h"

#include <QTimer>
#include <QWidget>
#include <memory>
#include <optional>
#include <random>

class QFrame;
class QLabel;
class QToolButton;

namespace scrabble {

class BoardView;
class Modal;
class RackView;

/**
 * @brief Main window: scoreboard, board, rack, and actions, plus every in-window dialog.
 *
 * Colors come from the theme file and reload live. The window background carries the theme's
 * opacity; all other surfaces are solid. The computer's moves, statistics, and the saved game
 * are handled here; the rules live in core/.
 */
class Window : public QWidget {
    Q_OBJECT
public:
    /**
     * @brief Create the window and resume the saved game, or start one.
     * @param level Opponent level override (easy, medium, hard).
     * @param seed Starts a new, reproducible game when set.
     */
    Window(const QString &configDirectory, const Theme &theme, std::optional<QString> level,
           std::optional<std::uint32_t> seed, QWidget *parent = nullptr);
    ~Window() override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool focusNextPrevChild(bool next) override;

private:
    struct PendingTile {
        Placement placement;
        int rackIndex = 0; ///< Index into the player's rack.
    };

    QString gamePath_, statsPath_;
    Theme theme_;
    ThemeWatcher watcher_;
    std::unique_ptr<Game> game_;
    Statistics statistics_;
    Level level_ = Level::Medium;
    std::mt19937 random_;
    bool statsRecorded_ = false;
    int bestPlay_ = 0;
    int hints_ = 0;

    std::vector<PendingTile> pending_;
    std::vector<int> order_; ///< Display order of rack slots (shuffle is cosmetic).
    int selectedRack_ = -1;
    int cursorRow_ = 7, cursorCol_ = 7;
    bool horizontal_ = true;
    bool busy_ = false; ///< The computer is taking its turn.
    /** @brief What the open modal is asking, so one connection per signal can route results. */
    enum class Ask { None, Blank, Pass, Exchange, NewGame, GameOver };
    Ask ask_ = Ask::None;
    std::optional<PendingTile> awaitingBlank_;
    QString status_;
    bool statusIsError_ = false;
    bool themeErrorShown_ = false;
    QTimer computerTimer_;

    QWidget *content_ = nullptr;
    BoardView *board_ = nullptr;
    RackView *rack_ = nullptr;
    Modal *modal_ = nullptr;
    QFrame *youCard_ = nullptr, *computerCard_ = nullptr, *side_ = nullptr;
    QLabel *youScore_ = nullptr, *computerScore_ = nullptr, *bag_ = nullptr, *statusLabel_ = nullptr, *history_ = nullptr;
    QToolButton *play_ = nullptr, *recall_ = nullptr, *shuffle_ = nullptr, *exchange_ = nullptr,
        *pass_ = nullptr, *hint_ = nullptr, *newGame_ = nullptr, *stats_ = nullptr, *rules_ = nullptr;

    void buildUi();
    void applyTheme();
    void themeChanged();
    void startGame(std::optional<std::uint32_t> seed);
    bool loadSavedGame();
    void saveGame();
    void refresh();
    void setStatus(const QString &text, bool error = false);
    bool humanTurn() const;

    void selectRackSlot(int slot);
    void clickSquare(int row, int col);
    void placeTile(int rackIndex, int row, int col, char blankLetter);
    void typeLetter(char letter);
    void recallLast();
    void recallAll();
    void moveCursor(int dRow, int dCol);
    void advanceCursor();
    bool pendingAt(int row, int col) const;
    std::vector<Placement> pendingPlacements() const;
    std::vector<bool> usedFlags() const;

    void submitPlay();
    void doShuffle();
    void doHint();
    void askPass();
    void askExchange();
    void askNewGame();
    void startComputerTurn();
    void computerMove();
    void afterTurn();
    void finishIfOver();
    QString describePlay(const LogEntry &entry) const;
};

}
