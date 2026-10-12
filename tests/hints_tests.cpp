/**
 * @file hints_tests.cpp
 * @brief Starting hints, earning rate, and restoring a saved bank for each level.
 */
#include "core/hints.h"

#include <QTest>

using namespace scrabble;

class HintTests : public QObject {
    Q_OBJECT
private slots:
    void startingHintsFollowTheLevel() {
        QCOMPARE(HintBank(Level::Easy).available(), 3);
        QCOMPARE(HintBank(Level::Medium).available(), 2);
        QCOMPARE(HintBank(Level::Hard).available(), 1);
    }

    void wordsNeededFollowTheLevel() {
        QCOMPARE(HintBank::wordsPerHint(Level::Easy), 5);
        QCOMPARE(HintBank::wordsPerHint(Level::Medium), 6);
        QCOMPARE(HintBank::wordsPerHint(Level::Hard), 7);
    }

    void aHintIsEarnedOnTheLastWordOfEachRun_data() {
        QTest::addColumn<int>("level");
        QTest::newRow("easy") << int(Level::Easy);
        QTest::newRow("medium") << int(Level::Medium);
        QTest::newRow("hard") << int(Level::Hard);
    }

    void aHintIsEarnedOnTheLastWordOfEachRun() {
        QFETCH(int, level);
        const auto chosen = static_cast<Level>(level);
        HintBank bank(chosen);
        const int start = bank.available();
        const int needed = HintBank::wordsPerHint(chosen);
        for (int round = 1; round <= 3; ++round) {
            for (int word = 1; word < needed; ++word) {
                QVERIFY(!bank.recordWord(chosen));
                QCOMPARE(bank.wordsUntilNext(chosen), needed - word);
            }
            QCOMPARE(bank.available(), start + round - 1);
            QVERIFY(bank.recordWord(chosen));
            QCOMPARE(bank.available(), start + round);
            QCOMPARE(bank.progress(), 0);
        }
    }

    void spendingStopsAtZero() {
        HintBank bank(Level::Hard);
        QVERIFY(bank.spend());
        QVERIFY(!bank.spend());
        QCOMPARE(bank.available(), 0);
    }

    void restoreValidatesRanges() {
        QVERIFY(HintBank::restore(2, 3, Level::Medium));
        QCOMPARE(HintBank::restore(2, 3, Level::Medium)->wordsUntilNext(Level::Medium), 3);
        QVERIFY(!HintBank::restore(-1, 0, Level::Easy));
        QVERIFY(!HintBank::restore(1, 5, Level::Easy));   // progress must be below the goal
        QVERIFY(!HintBank::restore(1, -1, Level::Easy));
        QVERIFY(!HintBank::restore(1000, 0, Level::Easy));
    }
};

QTEST_APPLESS_MAIN(HintTests)
#include "hints_tests.moc"
