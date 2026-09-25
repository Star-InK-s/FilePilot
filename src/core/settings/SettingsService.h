#pragma once

#include <QByteArray>
#include <QSettings>
#include <QString>

namespace FilePilot {

class SettingsService
{
public:
    explicit SettingsService(const QString &settingsFilePath = QString());

    QString settingsFilePath() const;

    QString lastDirectory() const;
    void setLastDirectory(const QString &directory);

    QString defaultBackupDirectory() const;
    void setDefaultBackupDirectory(const QString &directory);

    int logLevelValue() const;
    void setLogLevelValue(int level);

    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray &geometry);

    QByteArray windowState() const;
    void setWindowState(const QByteArray &state);

    void sync();

private:
    QSettings settings_;
};

} // namespace FilePilot
