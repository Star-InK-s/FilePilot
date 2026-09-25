#pragma once

#include "core/logging/LogManager.h"
#include "core/settings/SettingsService.h"

#include <QApplication>
#include <QString>

namespace FilePilot {

class Application : public QApplication
{
public:
    Application(int &argc,
                char **argv,
                const QString &dataDirectory = QString());
    ~Application() override;

    SettingsService &settings();
    const SettingsService &settings() const;
    LogManager &logger();
    const LogManager &logger() const;
    QString dataDirectory() const;

private:
    QString dataDirectory_;
    SettingsService settings_;
    LogManager logger_;
};

} // namespace FilePilot
