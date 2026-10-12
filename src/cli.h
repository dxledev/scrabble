#pragma once

#include <QStringList>
#include <cstdint>
#include <optional>

namespace scrabble {

struct Options {
    QString configDirectory;
    QStringList commands;
    std::optional<QString> level; ///< Opponent strength: easy, medium, or hard.
    std::optional<std::uint32_t> seed;
    bool help = false;
    bool version = false;
    bool dryRun = false;
};

Options parseOptions(const QStringList &arguments);
void printHelp();
int runThemeCommand(const Options &options);

}
