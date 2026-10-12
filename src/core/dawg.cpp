/**
 * @file dawg.cpp
 * @brief Lookup over the packed word graph and the embedded ENABLE dictionary.
 */

#include "core/dawg.h"

#include <QFile>
#include <QResource>

#include <cstring>
#include <stdexcept>

/** Forces the static-library resource object for the dictionary to be linked in. */
static void initDictionaryResource() {
    Q_INIT_RESOURCE(dictionary);
}

namespace scrabble {
namespace {

/** Reads the embedded ENABLE edge array into owned memory; throws when it is missing or invalid. */
std::vector<std::uint32_t> loadEnglish() {
    initDictionaryResource();
    QFile file(QStringLiteral(":/dict/enable.dawg"));
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("Word list missing from resources");
    }
    const QByteArray bytes = file.readAll();
    if (bytes.isEmpty() || bytes.size() % sizeof(std::uint32_t) != 0) {
        throw std::runtime_error("Word list missing from resources");
    }
    std::vector<std::uint32_t> edges(static_cast<std::size_t>(bytes.size()) / sizeof(std::uint32_t));
    std::memcpy(edges.data(), bytes.constData(), static_cast<std::size_t>(bytes.size()));
    if (!Dawg(edges.data(), edges.size()).valid()) {
        throw std::runtime_error("Word list missing from resources");
    }
    return edges;
}

}  // namespace

Dawg::Dawg(const std::uint32_t *edges, std::size_t count) : edges_(edges), count_(count) {}

const Dawg &Dawg::english() {
    static const std::vector<std::uint32_t> storage = loadEnglish();
    static const Dawg dawg(storage.data(), storage.size());
    return dawg;
}

Dawg::Edge Dawg::edge(std::uint32_t index) const {
    const std::uint32_t bits = edges_[index];
    Edge out;
    out.letter = static_cast<char>('A' + (bits & 0x1f));
    out.terminal = ((bits >> 5) & 1u) != 0;
    out.last = ((bits >> 6) & 1u) != 0;
    out.child = bits >> 7;
    return out;
}

bool Dawg::find(std::uint32_t node, char letter, Edge &out) const {
    if (node == 0 || !edges_) {
        return false;
    }
    for (std::size_t index = node; index < count_; ++index) {
        Edge candidate = edge(static_cast<std::uint32_t>(index));
        if (candidate.letter == letter) {
            out = candidate;
            return true;
        }
        if (candidate.last) {
            break;
        }
    }
    return false;
}

bool Dawg::contains(std::string_view word) const {
    if (!valid() || word.empty()) {
        return false;
    }
    std::uint32_t node = root();
    Edge current;
    for (std::size_t i = 0; i < word.size(); ++i) {
        const char c = word[i];
        if (c < 'A' || c > 'Z' || !find(node, c, current)) {
            return false;
        }
        if (i + 1 == word.size()) {
            return current.terminal;
        }
        node = current.child;
    }
    return false;
}

}  // namespace scrabble
