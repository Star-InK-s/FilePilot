#include "app/Application.h"

#include <QDir>
#include <QStandardPaths>

namespace FilePilot {

namespace {

QString resolveDataDirectory(const QString &dataDirectory)
{
    if (!dataDirectory.isEmpty()) {
        QDir().mkpath(dataDirectory);
        return QDir(dataDirectory).absolutePath();
    }

    const QString directory =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(directory);
    return QDir(directory).absolutePath();
}

LogLevel resolveLogLevel(const int value)
{
    switch (value) {
    case 0:
        return LogLevel::Debug;
    case 1:
        return LogLevel::Info;
    case 2:
        return LogLevel::Warning;
    case 3:
        return LogLevel::Error;
    default:
        return LogLevel::Info;
    }
}

} // namespace

Application::Application(int &argc, char **argv, const QString &dataDirectory)
    : QApplication(argc, argv)
    , dataDirectory_(resolveDataDirectory(dataDirectory))
    , settings_(QDir(dataDirectory_).filePath(QStringLiteral("settings.ini")))
    , logger_(QDir(dataDirectory_).filePath(QStringLiteral("logs/filepilot.log")),
              resolveLogLevel(settings_.logLevelValue()))
    , historyRepository_(QDir(dataDirectory_).filePath(QStringLiteral("history.sqlite")))
{
    setApplicationName(QStringLiteral("FilePilot"));
    setApplicationDisplayName(QStringLiteral("FilePilot"));
    setApplicationVersion(QStringLiteral("0.1.0"));
    setOrganizationName(QStringLiteral("FilePilot"));

    logger_.log(LogLevel::Info,
                QStringLiteral("Application"),
                QStringLiteral("Application initialized"));

    QString historyError;
    if (!historyRepository_.initialize(&historyError)) {
        logger_.log(LogLevel::Error,
                    QStringLiteral("HistoryRepository"),
                    QStringLiteral("History database initialization failed: %1")
                        .arg(historyError));
    }
}

Application::~Application()
{
    logger_.log(LogLevel::Info,
                QStringLiteral("Application"),
                QStringLiteral("Application shutting down"));
    settings_.sync();
}

SettingsService &Application::settings()
{
    return settings_;
}

const SettingsService &Application::settings() const
{
    return settings_;
}

LogManager &Application::logger()
{
    return logger_;
}

const LogManager &Application::logger() const
{
    return logger_;
}

ExecutionHistoryRepository &Application::historyRepository()
{
    return historyRepository_;
}

const ExecutionHistoryRepository &Application::historyRepository() const
{
    return historyRepository_;
}

QString Application::dataDirectory() const
{
    return dataDirectory_;
}

} // namespace FilePilot
