#include "core/settings/SettingsService.h"

#include <QDir>
#include <QStandardPaths>

namespace FilePilot {

namespace {

QString defaultSettingsFilePath()
{
    const QString dataDirectory =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dataDirectory);
    return QDir(dataDirectory).filePath(QStringLiteral("settings.ini"));
}

} // namespace

SettingsService::SettingsService(const QString &settingsFilePath)
    : settings_(settingsFilePath.isEmpty() ? defaultSettingsFilePath() : settingsFilePath,
                QSettings::IniFormat)
{
}

QString SettingsService::settingsFilePath() const
{
    return settings_.fileName();
}

QString SettingsService::lastDirectory() const
{
    return settings_.value(QStringLiteral("paths/lastDirectory")).toString();
}

void SettingsService::setLastDirectory(const QString &directory)
{
    settings_.setValue(QStringLiteral("paths/lastDirectory"), directory);
}

QString SettingsService::defaultBackupDirectory() const
{
    return settings_.value(QStringLiteral("backup/defaultDirectory")).toString();
}

void SettingsService::setDefaultBackupDirectory(const QString &directory)
{
    settings_.setValue(QStringLiteral("backup/defaultDirectory"), directory);
}

int SettingsService::logLevelValue() const
{
    return settings_.value(QStringLiteral("logging/level"), 1).toInt();
}

void SettingsService::setLogLevelValue(const int level)
{
    settings_.setValue(QStringLiteral("logging/level"), level);
}

QByteArray SettingsService::windowGeometry() const
{
    return settings_.value(QStringLiteral("window/geometry")).toByteArray();
}

void SettingsService::setWindowGeometry(const QByteArray &geometry)
{
    settings_.setValue(QStringLiteral("window/geometry"), geometry);
}

QByteArray SettingsService::windowState() const
{
    return settings_.value(QStringLiteral("window/state")).toByteArray();
}

void SettingsService::setWindowState(const QByteArray &state)
{
    settings_.setValue(QStringLiteral("window/state"), state);
}

void SettingsService::sync()
{
    settings_.sync();
}

} // namespace FilePilot
