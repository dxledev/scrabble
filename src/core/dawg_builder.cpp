/**
 * @file dawg_builder.cpp
 * @brief Builds the minimized packed edge array consumed by scrabble::Dawg.
 */

#include "core/dawg.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>

namespace scrabble {
namespace {

constexpr std::uint32_t kMaxChild = (1u << 25) - 1;

struct TrieEdge {
    std::uint32_t letter;  // 0..25
    bool terminal;
    std::size_t child;     // index into the trie node table
};

using TrieNode = std::vector<TrieEdge>;  // sorted by letter

/** Inserts @p word into the trie, creating edges in letter order. */
void insertWord(std::vector<TrieNode> &trie, const std::string &word) {
    std::size_t node = 0;
    for (std::size_t i = 0; i < word.size(); ++i) {
        auto letter = static_cast<std::uint32_t>(word[i] - 'A');
        std::size_t pos = 0;
        while (pos < trie[node].size() && trie[node][pos].letter < letter) {
            ++pos;
        }
        if (pos == trie[node].size() || trie[node][pos].letter != letter) {
            trie[node].insert(trie[node].begin() + static_cast<std::ptrdiff_t>(pos),
                              TrieEdge{letter, false, trie.size()});
            trie.emplace_back();
        }
        if (i + 1 == word.size()) {
            trie[node][pos].terminal = true;
        }
        node = trie[node][pos].child;
    }
}

/**
 * Assigns each trie node a class id by its signature (letter, terminal, child class),
 * so identical sub-tries share one id. Class 0 is the empty node (no children).
 */
class Minimizer {
public:
    explicit Minimizer(const std::vector<TrieNode> &trie) : trie_(trie), memo_(trie.size(), kUnset) {
        classes_.emplace_back();  // class 0: no children
    }

    std::size_t classify(std::size_t node) {
        if (memo_[node] != kUnset) {
            return memo_[node];
        }
        std::vector<std::uint64_t> signature;
        signature.reserve(trie_[node].size());
        for (const TrieEdge &edge : trie_[node]) {
            std::uint64_t child = classify(edge.child);
            signature.push_back((child << 6) | (edge.terminal ? 1u << 5 : 0u) | edge.letter);
        }
        std::size_t id = 0;
        if (!signature.empty()) {
            auto [it, inserted] = ids_.try_emplace(signature, classes_.size());
            if (inserted) {
                classes_.push_back(signature);
            }
            id = it->second;
        }
        memo_[node] = id;
        return id;
    }

    const std::vector<std::vector<std::uint64_t>> &classes() const { return classes_; }

private:
    static constexpr std::size_t kUnset = static_cast<std::size_t>(-1);
    const std::vector<TrieNode> &trie_;
    std::vector<std::size_t> memo_;
    std::map<std::vector<std::uint64_t>, std::size_t> ids_;
    std::vector<std::vector<std::uint64_t>> classes_;
};

/**
 * Emits the sibling list of class @p id and, recursively, the lists it points to.
 * The list is reserved before its children are emitted, so each list is contiguous.
 * @return Index of the list's first edge.
 */
std::uint32_t emit(std::size_t id, const std::vector<std::vector<std::uint64_t>> &classes,
                   std::vector<std::uint32_t> &out, std::vector<std::uint32_t> &starts) {
    const auto &signature = classes[id];
    auto start = static_cast<std::uint32_t>(out.size());
    out.resize(out.size() + signature.size(), 0);
    starts[id] = start;
    for (std::size_t i = 0; i < signature.size(); ++i) {
        std::uint64_t value = signature[i];
        auto childClass = static_cast<std::size_t>(value >> 6);
        std::uint32_t child = 0;
        if (childClass != 0) {
            child = starts[childClass] != 0 ? starts[childClass] : emit(childClass, classes, out, starts);
        }
        if (child > kMaxChild) {
            throw std::length_error("Word graph exceeds the 25-bit child index limit");
        }
        std::uint32_t bits = static_cast<std::uint32_t>(value & 0x3f);
        std::uint32_t last = i + 1 == signature.size() ? 1u << 6 : 0u;
        out[start + i] = (child << 7) | last | bits;
    }
    return start;
}

}  // namespace

/**
 * Words are sorted and deduplicated, inserted into a trie, then minimized bottom-up.
 * Empty input yields {0, 0}: the sentinel plus one zero edge that Dawg::contains never
 * accepts (valid() is true, but no word can be found).
 */
std::vector<std::uint32_t> buildDawgEdges(std::vector<std::string> words) {
    std::sort(words.begin(), words.end());
    words.erase(std::unique(words.begin(), words.end()), words.end());

    std::vector<TrieNode> trie(1);
    for (const std::string &word : words) {
        if (!word.empty()) {
            insertWord(trie, word);
        }
    }

    Minimizer minimizer(trie);
    std::size_t root = minimizer.classify(0);
    const auto &classes = minimizer.classes();

    std::vector<std::uint32_t> out{0};
    if (root == 0) {
        out.push_back(0);
        return out;
    }
    std::vector<std::uint32_t> starts(classes.size(), 0);
    emit(root, classes, out, starts);
    return out;
}

}  // namespace scrabble
