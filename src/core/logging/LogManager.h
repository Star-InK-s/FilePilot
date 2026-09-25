#pragma once

#include <QFile>
#include <QMutex>
#include <QString>

namespace FilePilot {

enum class LogLevel {
    Debug = 0,
    Info = 1,
    Warning = 2,
    Error = 3
};

class LogManager
{
public:
    explicit LogManager(QString logFilePath, LogLevel level = LogLevel::Info);
    ~LogManager();

    LogManager(const LogManager &) = delete;
    LogManager &operator=(const LogManager &) = delete;

    QString logFilePath() const;
    bool isAvailable() const;
    LogLevel logLevel() const;
    void setLogLevel(LogLevel level);
    qint64 entryCount() const;

    bool log(LogLevel level, const QString &category, const QString &message);
    void flush();

private:
    QString levelName(LogLevel level) const;

    QString logFilePath_;
    QFile file_;
    LogLevel level_ = LogLevel::Info;
    qint64 entryCount_ = 0;
    mutable QMutex mutex_;
};

} // namespace FilePilot
