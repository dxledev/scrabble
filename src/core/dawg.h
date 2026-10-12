#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace scrabble {

/**
 * @brief Read-only directed acyclic word graph stored as a packed array of 32-bit edges.
 *
 * Edge layout (least significant bit first): bits 0-4 letter (0 = 'A' .. 25 = 'Z'),
 * bit 5 terminal (the path ending with this edge spells a word), bit 6 last (final edge in
 * its sibling list), bits 7-31 child (index of the first edge of the child's sibling list,
 * or 0 when the edge leads nowhere). Index 0 is a reserved all-zero sentinel and the root's
 * sibling list starts at index 1. A "node" is the index of the first edge in its list; node
 * 0 means "no children". Words are uppercase 'A'-'Z'.
 */
class Dawg {
public:
    struct Edge {
        char letter = 0;        ///< 'A'-'Z'.
        bool terminal = false;  ///< Path through this edge ends a word.
        bool last = false;      ///< Final sibling in the list.
        std::uint32_t child = 0;///< First edge of the child list; 0 when none.
    };

    Dawg() = default;
    /** @brief View @p count edges at @p edges. The memory must outlive the Dawg. */
    Dawg(const std::uint32_t *edges, std::size_t count);

    /** @brief Embedded English word list (ENABLE), loaded once on first use; throws std::runtime_error. */
    static const Dawg &english();

    /** @brief True when the view has the sentinel and at least one edge. */
    bool valid() const { return edges_ && count_ >= 2 && edges_[0] == 0; }
    /** @brief Node of the root sibling list. */
    std::uint32_t root() const { return 1; }
    /** @brief Decode the edge at @p index (must be < size). */
    Edge edge(std::uint32_t index) const;
    /** @brief Find the edge for @p letter in the sibling list starting at @p node. False when absent or node is 0. */
    bool find(std::uint32_t node, char letter, Edge &out) const;
    /** @brief Exact-word lookup for an uppercase A-Z string (length >= 1). */
    bool contains(std::string_view word) const;
    /** @brief Number of edges including the sentinel. */
    std::size_t size() const { return count_; }

private:
    const std::uint32_t *edges_ = nullptr;
    std::size_t count_ = 0;
};

/**
 * @brief Build a minimized packed edge array from words.
 * @param words Uppercase A-Z words, any order; duplicates and empty strings are ignored.
 * @return Edge array in the Dawg layout, beginning with the zero sentinel.
 */
std::vector<std::uint32_t> buildDawgEdges(std::vector<std::string> words);

}
