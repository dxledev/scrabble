/**
 * @file dawg_tests.cpp
 * @brief Tests for the packed word graph builder, lookup, and the embedded ENABLE dictionary.
 */

#include "core/dawg.h"

#include <QFile>
#include <QFileInfo>
#include <QTest>

#include <cctype>
#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using scrabble::buildDawgEdges;
using scrabble::Dawg;

namespace {

/** Builds the edge array for @p words. */
std::vector<std::uint32_t> build(std::vector<std::string> words) {
    return buildDawgEdges(std::move(words));
}

/** True when @p word is in the graph spanned by @p edges. */
bool has(const std::vector<std::uint32_t> &edges, std::string_view word) {
    return Dawg(edges.data(), edges.size()).contains(word);
}

}  // namespace

class DawgTests : public QObject {
    Q_OBJECT

private slots:
    void smallListContains();
    void minimizationSharesSuffixes();
    void inputOrderAndDuplicatesIgnored();
    void emptyInputContainsNothing();
    void englishContainsKnownWords();
    void englishRejectsNonWords();
    void englishMatchesSourceList();
};

void DawgTests::smallListContains() {
    const auto edges = build({"CAT", "CATS", "CAR", "CARE", "DOG"});
    QVERIFY(has(edges, "CAT"));
    QVERIFY(has(edges, "CATS"));
    QVERIFY(has(edges, "CAR"));
    QVERIFY(has(edges, "CARE"));
    QVERIFY(has(edges, "DOG"));
    QVERIFY(!has(edges, "CA"));
    QVERIFY(!has(edges, "CART"));
    QVERIFY(!has(edges, "DOGS"));
    QVERIFY(!has(edges, ""));
    QVERIFY(!has(edges, "cat"));
    QVERIFY(!has(edges, "Cat"));
}

void DawgTests::minimizationSharesSuffixes() {
    const std::vector<std::string> words{"WALKED", "TALKED", "WALKING", "TALKING"};
    std::set<std::string> prefixes;
    for (const std::string &word : words) {
        for (std::size_t n = 1; n <= word.size(); ++n) {
            prefixes.insert(word.substr(0, n));
        }
    }
    const auto edges = build(words);
    QVERIFY(edges.size() - 1 < prefixes.size());
    for (const std::string &word : words) {
        QVERIFY(has(edges, word));
    }
    QVERIFY(!has(edges, "WALK"));
    QVERIFY(!has(edges, "TALKS"));
}

void DawgTests::inputOrderAndDuplicatesIgnored() {
    const auto sorted = build({"CAR", "CAT", "DOG"});
    QVERIFY(build({"DOG", "CAT", "CAR", "CAT", "", "DOG"}) == sorted);
    QVERIFY(build({"CAT", "", "DOG", "CAR", "CAR"}) == sorted);
}

void DawgTests::emptyInputContainsNothing() {
    const auto edges = build({});
    QVERIFY(Dawg(edges.data(), edges.size()).valid());
    QVERIFY(!has(edges, "A"));
    QVERIFY(!has(edges, "CAT"));
    QVERIFY(!Dawg().contains("CAT"));
}

void DawgTests::englishContainsKnownWords() {
    const Dawg &dawg = Dawg::english();
    QVERIFY(dawg.valid());
    for (const char *word : {"QUIZ", "AA", "JOKE", "XYLOPHONES"}) {
        QVERIFY2(dawg.contains(word), word);
    }
}

void DawgTests::englishRejectsNonWords() {
    const Dawg &dawg = Dawg::english();
    for (const char *word : {"QZX", "AAAA", "A", "Z", "", "quiz", "QU1Z"}) {
        QVERIFY2(!dawg.contains(word), word);
    }
}

void DawgTests::englishMatchesSourceList() {
    // Source directory is derived from this file's path; skip when the data file is not beside it.
    const QString listPath = QFileInfo(QStringLiteral(__FILE__)).absolutePath() + "/../data/enable1.txt";
    QFile list(listPath);
    if (!list.open(QIODevice::ReadOnly)) {
        QSKIP("data/enable1.txt not found next to the source tree");
    }
    std::vector<std::string> lines;
    while (!list.atEnd()) {
        std::string word = list.readLine().trimmed().toStdString();
        for (char &c : word) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        lines.push_back(std::move(word));
    }

    const Dawg &dawg = Dawg::english();
    constexpr std::size_t kSamples = 2000;
    const std::size_t stride = lines.size() / kSamples;
    std::size_t checked = 0;
    for (std::size_t i = 0; i < lines.size(); i += stride) {
        const std::string &word = lines[i];
        if (word.size() < 2 || word.size() > 15) {
            continue;
        }
        QVERIFY2(dawg.contains(word), word.c_str());
        ++checked;
    }
    QVERIFY(checked >= kSamples * 9 / 10);
}

QTEST_APPLESS_MAIN(DawgTests)
#include "dawg_tests.moc"
