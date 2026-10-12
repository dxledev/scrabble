/**
 * @file modals.cpp
 * @brief In-window dialogs: messages, blank-letter picker, exchange, new game, stats, rules.
 *
 * Every dialog is a child layer of the main window painted over a blurred snapshot. The card
 * is rebuilt for each request; no native popup, dialog, tooltip, or menu surface is created.
 */
#include "modals.h"
#include "tile_painter.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QStyle>
#include <QVBoxLayout>
#include <algorithm>

namespace scrabble {

TileButton::TileButton(char letter, const Theme &theme, QWidget *parent)
    : QAbstractButton(parent), letter_(letter), theme_(theme) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(letter == '?' ? QString("Blank tile") : QString("Tile %1").arg(QChar(letter)));
}

void TileButton::setTheme(const Theme &theme) {
    theme_ = theme;
    update();
}

void TileButton::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF area = QRectF(rect()).adjusted(3, 3, -3, -3);
    // A blank in the rack is picked as a plain tile; a stand-in letter is shown as a blank.
    drawTile(painter, area, letter_, false, theme_, isChecked() ? TileState::Selected : TileState::Normal);
    if (hasFocus() && !isChecked()) {
        painter.setPen(QPen(theme_.color("accent"), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 5, 5);
    }
}

Modal::Modal(QWidget *parent, QWidget *background) : ModalBackdrop(parent, background) {
    setObjectName("modal");
    setAccessibleName("Dialog");
    setFocusPolicy(Qt::StrongFocus);
    setStyleSheet("QWidget#modal { background: transparent; }");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->addStretch();
    card_ = new QFrame(this);
    layout->addWidget(card_, 0, Qt::AlignHCenter);
    layout->addStretch();
    connect(this, &ModalBackdrop::outsideClicked, this, &Modal::dismiss);
    hide();
}

void Modal::setTheme(const Theme &theme) {
    theme_ = theme;
    setTint(theme.color("background"));
    for (auto *button : card_->findChildren<TileButton *>())
        button->setTheme(theme);
}

QVBoxLayout *Modal::beginCard(const QString &title, int width) {
    auto *old = card_;
    auto *card = new QFrame(this);
    card->setObjectName("modalCard");
    static_cast<QVBoxLayout *>(layout())->insertWidget(1, card, 0, Qt::AlignHCenter);
    layout()->removeWidget(old);
    old->deleteLater();
    old->hide();
    card_ = card;
    cardWidth_ = width;
    lettersOnly_ = false;
    defaultButton_ = nullptr;
    cardLayout_ = new QVBoxLayout(card);
    cardLayout_->setContentsMargins(24, 22, 24, 22);
    cardLayout_->setSpacing(16);
    auto *heading = new QLabel(title);
    heading->setObjectName("modalTitle");
    heading->setAlignment(Qt::AlignCenter);
    heading->setWordWrap(true);
    cardLayout_->addWidget(heading);
    return cardLayout_;
}

QList<QAbstractButton *> Modal::addButtons(const QStringList &labels, int primaryIndex) {
    auto *row = new QHBoxLayout;
    row->setSpacing(10);
    row->addStretch();
    QList<QAbstractButton *> buttons;
    for (int index = 0; index < labels.size(); ++index) {
        auto *button = new QPushButton(labels[index]);
        button->setCursor(Qt::PointingHandCursor);
        button->setProperty("primary", index == primaryIndex);
        row->addWidget(button);
        buttons.append(button);
        if (index == primaryIndex)
            defaultButton_ = button;
    }
    row->addStretch();
    cardLayout_->addLayout(row);
    if (!defaultButton_ && !buttons.isEmpty())
        defaultButton_ = buttons.first();
    return buttons;
}

void Modal::fitCard() {
    card_->setFixedWidth(std::min(cardWidth_, std::max(0, width() - 48)));
    layout()->activate();
}

void Modal::display(QAbstractButton *focus) {
    fitCard();
    show();
    raise();
    (focus ? static_cast<QWidget *>(focus) : static_cast<QWidget *>(this))->setFocus();
    // Style sheets apply on first polish; refresh so the primary property takes effect.
    style()->unpolish(card_);
    style()->polish(card_);
    for (auto *button : card_->findChildren<QPushButton *>()) {
        style()->unpolish(button);
        style()->polish(button);
    }
}

void Modal::dismiss() {
    hide();
    emit dismissed();
}

void Modal::showMessage(const QString &title, const QString &body, const QStringList &buttons, int primaryIndex) {
    beginCard(title, 460);
    if (!body.isEmpty()) {
        auto *text = new QLabel(body);
        text->setObjectName("modalBody");
        text->setAlignment(Qt::AlignCenter);
        text->setWordWrap(true);
        cardLayout_->addWidget(text);
    }
    const auto row = addButtons(buttons, primaryIndex);
    for (int index = 0; index < row.size(); ++index) {
        connect(row[index], &QAbstractButton::clicked, this, [this, index] {
            hide();
            emit answered(index);
        });
    }
    display(defaultButton_);
}

void Modal::showLetterPicker() {
    beginCard("Choose a letter for the blank", 520);
    auto *grid = new QGridLayout;
    grid->setSpacing(6);
    for (int index = 0; index < 26; ++index) {
        auto *tile = new TileButton(char('A' + index), theme_);
        tile->setCheckable(false);
        tile->setFixedSize(44, 44);
        connect(tile, &QAbstractButton::clicked, this, [this, index] {
            hide();
            emit letterChosen(char('A' + index));
        });
        grid->addWidget(tile, index / 7, index % 7);
    }
    grid->setAlignment(Qt::AlignCenter);
    cardLayout_->addLayout(grid);
    auto *hint = new QLabel("Click a letter or press its key");
    hint->setObjectName("modalBody");
    hint->setAlignment(Qt::AlignCenter);
    cardLayout_->addWidget(hint);
    const auto row = addButtons({"Cancel"}, -1);
    connect(row[0], &QAbstractButton::clicked, this, &Modal::dismiss);
    lettersOnly_ = true;
    display(nullptr);
}

void Modal::showExchange(const std::vector<char> &rack) {
    beginCard("Exchange tiles", 520);
    auto *text = new QLabel("Select the tiles to swap. Exchanging ends your turn without scoring.");
    text->setObjectName("modalBody");
    text->setAlignment(Qt::AlignCenter);
    text->setWordWrap(true);
    cardLayout_->addWidget(text);
    auto *tiles = new QHBoxLayout;
    tiles->setSpacing(6);
    tiles->addStretch();
    std::vector<TileButton *> buttons;
    for (const char letter : rack) {
        auto *tile = new TileButton(letter, theme_);
        tile->setFixedSize(48, 48);
        tiles->addWidget(tile);
        buttons.push_back(tile);
    }
    tiles->addStretch();
    cardLayout_->addLayout(tiles);
    const auto row = addButtons({"Cancel", "Exchange"}, 1);
    row[1]->setEnabled(false);
    for (auto *tile : buttons) {
        connect(tile, &QAbstractButton::toggled, this, [buttons, button = row[1]] {
            button->setEnabled(std::any_of(buttons.begin(), buttons.end(), [](auto *item) { return item->isChecked(); }));
        });
    }
    connect(row[0], &QAbstractButton::clicked, this, &Modal::dismiss);
    connect(row[1], &QAbstractButton::clicked, this, [this, buttons] {
        std::vector<char> chosen;
        for (auto *tile : buttons) {
            if (tile->isChecked())
                chosen.push_back(tile->letter());
        }
        hide();
        emit exchangeChosen(chosen);
    });
    display(buttons.empty() ? nullptr : buttons.front());
}

void Modal::showNewGame(int currentLevel, bool inProgress) {
    beginCard("New game", 460);
    auto *text = new QLabel(inProgress ? "Your current game will be replaced. Choose how strong the computer plays."
                                       : "Choose how strong the computer plays.");
    text->setObjectName("modalBody");
    text->setAlignment(Qt::AlignCenter);
    text->setWordWrap(true);
    cardLayout_->addWidget(text);
    auto *levels = new QHBoxLayout;
    levels->setSpacing(8);
    levels->addStretch();
    std::vector<QPushButton *> choices;
    const QStringList names{"Easy", "Medium", "Hard"};
    for (int index = 0; index < 3; ++index) {
        auto *choice = new QPushButton(names[index]);
        choice->setCheckable(true);
        choice->setChecked(index == std::clamp(currentLevel, 0, 2));
        choice->setProperty("choice", true);
        choice->setCursor(Qt::PointingHandCursor);
        levels->addWidget(choice);
        choices.push_back(choice);
    }
    levels->addStretch();
    for (auto *choice : choices) {
        connect(choice, &QPushButton::clicked, this, [choices, choice] {
            for (auto *other : choices)
                other->setChecked(other == choice);
        });
    }
    cardLayout_->addLayout(levels);
    const auto row = addButtons({"Cancel", "Start"}, 1);
    connect(row[0], &QAbstractButton::clicked, this, &Modal::dismiss);
    connect(row[1], &QAbstractButton::clicked, this, [this, choices] {
        int level = 1;
        for (int index = 0; index < int(choices.size()); ++index)
            if (choices[index]->isChecked())
                level = index;
        hide();
        emit newGameChosen(level);
    });
    display(row[1]);
}

void Modal::showStats(const Statistics &statistics) {
    beginCard("Your stats", 560);
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(8);
    const QStringList headers{"", "Easy", "Medium", "Hard"};
    for (int column = 0; column < headers.size(); ++column) {
        auto *label = new QLabel(headers[column]);
        label->setObjectName("modalColumn");
        label->setAlignment(column ? Qt::AlignRight : Qt::AlignLeft);
        grid->addWidget(label, 0, column);
    }
    const QStringList rows{"Games", "Wins", "Losses", "Ties", "Win rate", "Average score", "Best score", "Best play"};
    for (int row = 0; row < rows.size(); ++row) {
        auto *name = new QLabel(rows[row]);
        name->setObjectName("modalBody");
        grid->addWidget(name, row + 1, 0);
        for (int level = 0; level < Statistics::kLevels; ++level) {
            const auto &stats = statistics.level(level);
            const int played = stats.games;
            const QStringList values{
                QString::number(stats.games), QString::number(stats.wins), QString::number(stats.losses),
                QString::number(stats.ties),
                played ? QString("%1%").arg(qRound(100.0 * stats.wins / played)) : "–",
                played ? QString::number(qRound(double(stats.totalScore) / played)) : "–",
                played ? QString::number(stats.bestScore) : "–", played ? QString::number(stats.bestPlay) : "–"};
            auto *value = new QLabel(values[row]);
            value->setObjectName("modalValue");
            value->setAlignment(Qt::AlignRight);
            grid->addWidget(value, row + 1, level + 1);
        }
    }
    cardLayout_->addLayout(grid);
    auto *note = new QLabel("Saved on this device only. Ties and unfinished games are not wins.");
    note->setObjectName("modalBody");
    note->setAlignment(Qt::AlignCenter);
    note->setWordWrap(true);
    cardLayout_->addWidget(note);
    const auto row = addButtons({"Close"}, 0);
    connect(row[0], &QAbstractButton::clicked, this, &Modal::dismiss);
    display(row[0]);
}

void Modal::showRules() {
    beginCard("How to play", 560);
    auto *text = new QLabel(
        "Place tiles in one row or column to form a word that connects to the board. "
        "The first word must cover the center star. Every word you form must be in the dictionary.\n\n"
        "Click a rack tile, then a square, or type letters at the cursor. Space flips the direction. "
        "Enter plays the word; Backspace recalls the last tile.\n\n"
        "2L / 3L multiply a tile, 2W / 3W multiply a word. Blank tiles score nothing. "
        "Using all seven tiles earns 50 bonus points. The game ends when a rack and the bag are empty "
        "or after six scoreless turns.");
    text->setObjectName("modalBody");
    text->setWordWrap(true);
    cardLayout_->addWidget(text);
    const auto row = addButtons({"Got it"}, 0);
    connect(row[0], &QAbstractButton::clicked, this, &Modal::dismiss);
    display(row[0]);
}

void Modal::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        dismiss();
    } else if (lettersOnly_ && event->key() >= Qt::Key_A && event->key() <= Qt::Key_Z) {
        hide();
        emit letterChosen(char('A' + event->key() - Qt::Key_A));
    } else if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && defaultButton_
               && defaultButton_->isEnabled() && !qobject_cast<TileButton *>(focusWidget())) {
        defaultButton_->click();
    } else {
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
}

bool Modal::focusNextPrevChild(bool next) {
    QList<QWidget *> chain;
    for (auto *button : card_->findChildren<QAbstractButton *>()) {
        if (button->isEnabled() && button->focusPolicy() != Qt::NoFocus)
            chain.append(button);
    }
    if (chain.isEmpty())
        return true;
    const int current = chain.indexOf(focusWidget());
    const int step = next ? 1 : -1;
    chain[(current < 0 ? (next ? 0 : chain.size() - 1) : (current + step + chain.size()) % chain.size())]->setFocus();
    return true;
}

void Modal::resizeEvent(QResizeEvent *event) {
    ModalBackdrop::resizeEvent(event);
    fitCard();
}

}
