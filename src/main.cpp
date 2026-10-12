/**
 * @file main.cpp
 * @brief Entry point for the game window and the theme command line.
 *
 * Options are parsed before either a core-only command process or the widget
 * application is created, so theme commands never open a window.
 */
#include "cli.h"
#include "core/storage.h"
#include "core/theme.h"
#include "ui/window.h"

#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QTextStream>
#include <stdexcept>

int main(int argc, char **argv) {
    QCoreApplication::setApplicationName("scrabble");
    QCoreApplication::setApplicationVersion("1.0.0");
    try {
        QStringList arguments;
        for (int index = 1; index < argc; ++index)
            arguments.append(QString::fromLocal8Bit(argv[index]));
        const auto options = scrabble::parseOptions(arguments);
        if (options.help || options.version || !options.commands.isEmpty()) {
            QCoreApplication app(argc, argv);
            if (options.help)
                scrabble::printHelp();
            else if (options.version)
                QTextStream(stdout) << "Scrabble 1.0.0\n";
            else
                return scrabble::runThemeCommand(options);
            return 0;
        }
        if (options.dryRun)
            throw std::invalid_argument("--dry-run requires a theme command");
        if (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
            qputenv("QT_QPA_PLATFORM", "wayland");
        // Raster painting uses shared memory instead of allocating GPU buffers.
        if (qEnvironmentVariableIsEmpty("QT_WAYLAND_CLIENT_BUFFER_INTEGRATION"))
            qputenv("QT_WAYLAND_CLIENT_BUFFER_INTEGRATION", "shm");
        QApplication app(argc, argv);
        app.setApplicationDisplayName("Scrabble");
        app.setDesktopFileName("io.github.scrabble");
        app.setStyle("Fusion");
        app.setWindowIcon(QIcon::fromTheme("io.github.scrabble"));
        auto theme = scrabble::ensureTheme(options.configDirectory + "/theme.json");
        scrabble::Window window(options.configDirectory, theme, options.level, options.seed);
        window.show();
        return app.exec();
    } catch (const std::exception &error) {
        QTextStream(stderr) << "scrabble: " << error.what() << '\n';
        return 1;
    }
}
