/**
 * @file mkdict.cpp
 * @brief Compiles a word list into the packed DAWG edge file embedded in the game.
 *
 * Usage: mkdict INPUT.txt OUTPUT.dawg
 */

#include "core/dawg.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kMinLength = 2;
constexpr std::size_t kMaxLength = 15;

/** Uppercases @p line and returns true when it is 2-15 letters A-Z only. */
bool normalize(std::string &line) {
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    if (line.size() < kMinLength || line.size() > kMaxLength) {
        return false;
    }
    for (char &c : line) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
        if (c < 'A' || c > 'Z') {
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "usage: mkdict INPUT.txt OUTPUT.dawg\n";
        return 2;
    }
    const std::filesystem::path output = argv[2];

    std::ifstream input(argv[1]);
    if (!input) {
        std::cerr << "mkdict: cannot read " << argv[1] << '\n';
        return 1;
    }

    std::vector<std::string> words;
    for (std::string line; std::getline(input, line);) {
        if (normalize(line)) {
            words.push_back(std::move(line));
        }
    }
    const std::size_t wordCount = words.size();

    const std::vector<std::uint32_t> edges = scrabble::buildDawgEdges(std::move(words));

    std::vector<unsigned char> bytes;
    bytes.reserve(edges.size() * sizeof(std::uint32_t));
    for (std::uint32_t edge : edges) {
        for (int shift = 0; shift < 32; shift += 8) {
            bytes.push_back(static_cast<unsigned char>(edge >> shift));
        }
    }

    std::filesystem::path temp = output;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            std::cerr << "mkdict: cannot write " << temp.string() << '\n';
            return 1;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temp, output, ec);
    if (ec) {
        std::cerr << "mkdict: cannot rename to " << output.string() << ": " << ec.message() << '\n';
        std::filesystem::remove(temp, ec);
        return 1;
    }

    std::cout << "words " << wordCount << ", edges " << edges.size() << '\n';
    return 0;
}
