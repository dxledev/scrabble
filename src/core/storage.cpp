/**
 * @file storage.cpp
 * @brief Atomic JSON persistence shared by themes, saved games, and statistics.
 */
#include "storage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>
#include <stdexcept>

namespace scrabble {
namespace {

[[noreturn]] void fail(const QString &message) {
    throw std::runtime_error(message.toStdString());
}

}

QString defaultConfigDirectory() {
    const QString override = qEnvironmentVariable("SCRABBLE_CONFIG_DIR");
    if (!override.isEmpty())
        return QDir(override).absolutePath();
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/scrabble";
}

QJsonObject readJson(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        fail(path + ": " + file.errorString());
    if (file.size() > 1024 * 1024)
        fail(path + ": JSON file exceeds 1 MiB");
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        fail(path + ": expected a valid JSON object (" + error.errorString() + ")");
    return document.object();
}

void writeJson(const QString &path, const QJsonObject &data, QJsonDocument::JsonFormat format) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        fail("Cannot create " + QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        fail(path + ": " + file.errorString());
    const auto bytes = QJsonDocument(data).toJson(format);
    if (file.write(bytes) != bytes.size() || !file.commit())
        fail(path + ": " + file.errorString());
}

}
