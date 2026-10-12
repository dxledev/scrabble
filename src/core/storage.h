#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace scrabble {

/** @brief Resolve the configuration directory, honoring SCRABBLE_CONFIG_DIR. */
QString defaultConfigDirectory();
/** @brief Read a JSON object from disk and throw on I/O, size, or format errors. */
QJsonObject readJson(const QString &path);
/** @brief Atomically write a JSON object, creating parent directories as needed. */
void writeJson(const QString &path, const QJsonObject &data, QJsonDocument::JsonFormat format = QJsonDocument::Indented);

}
