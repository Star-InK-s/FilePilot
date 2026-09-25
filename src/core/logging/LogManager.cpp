#include "core/logging/LogManager.h"

#include <QDateTime>
#include <QFileInfo>
#include <QDir>
#include <QMutexLocker>
#include <QTextStream>

#include <utility>

namespace FilePilot {

LogManager::LogManager(QString logFilePath, const LogLevel level)
    : logFilePath_(std::move(logFilePath))
    , level_(level)
{
    QDir().mkpath(QFileInfo(logFilePath_).absolutePath());
    file_.setFileName(logFilePath_);
    if (!file_.open(QIODevice::Append | QIODevice::WriteOnly | QIODevice::Text)) {
        file_.close();
    }
}

LogManager::~LogManager()
{
    flush();
}

QString LogManager::logFilePath() const
{
    QMutexLocker locker(&mutex_);
    return logFilePath_;
}

bool LogManager::isAvailable() const
{
    QMutexLocker locker(&mutex_);
    return file_.isOpen();
}

LogLevel LogManager::logLevel() const
{
    QMutexLocker locker(&mutex_);
    return level_;
}

void LogManager::setLogLevel(const LogLevel level)
{
    QMutexLocker locker(&mutex_);
    level_ = level;
}

qint64 LogManager::entryCount() const
{
    QMutexLocker locker(&mutex_);
    return entryCount_;
}

bool LogManager::log(const LogLevel level,
                     const QString &category,
                     const QString &message)
{
    QMutexLocker locker(&mutex_);
    if (!file_.isOpen() || static_cast<int>(level) < static_cast<int>(level_)) {
        return false;
    }

    QTextStream stream(&file_);
    stream << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
           << QStringLiteral(" [") << levelName(level) << QStringLiteral("] ")
           << category << QStringLiteral(": ") << message << '\n';
    stream.flush();

    ++entryCount_;
    return true;
}

void LogManager::flush()
{
    QMutexLocker locker(&mutex_);
    if (file_.isOpen()) {
        file_.flush();
    }
}

QString LogManager::levelName(const LogLevel level) const
{
    switch (level) {
    case LogLevel::Debug:
        return QStringLiteral("DEBUG");
    case LogLevel::Info:
        return QStringLiteral("INFO");
    case LogLevel::Warning:
        return QStringLiteral("WARN");
    case LogLevel::Error:
        return QStringLiteral("ERROR");
    }

    return QStringLiteral("UNKNOWN");
}

} // namespace FilePilot

