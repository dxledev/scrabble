#pragma once

#include "core/theme.h"
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

namespace scrabble {

/** @brief Debounced watcher that reloads valid theme file updates at runtime. */
class ThemeWatcher : public QObject {
    Q_OBJECT
public:
    /** @brief Watch @p path and begin with an already validated theme. */
    ThemeWatcher(QString path, Theme initial, QObject *parent = nullptr);
    /** @brief Return the last accepted theme. */
    const Theme &theme() const { return theme_; }
    /** @brief Return the output theme file path. */
    const QString &path() const { return path_; }

signals:
    /** @brief Emitted after a changed file produces a different valid theme. */
    void changed();
    /** @brief Emitted when a source update is invalid; the last valid theme remains active. */
    void rejected(const QString &message);
    /** @brief Emitted when a reload succeeds after one or more rejected updates, even if the colors are unchanged. */
    void recovered();

private:
    QString path_;
    Theme theme_;
    QFileSystemWatcher watcher_;
    QTimer debounce_;
    bool rejected_ = false;
    /** @brief Reload the theme and preserve the last valid palette on failure. */
    void reload();
    /** @brief Synchronize file and directory watches with the current source. */
    void updateWatches();
};

}
