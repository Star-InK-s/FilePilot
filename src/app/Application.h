#pragma once

#include "core/database/ExecutionHistoryRepository.h"
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
    ExecutionHistoryRepository &historyRepository();
    const ExecutionHistoryRepository &historyRepository() const;
    QString dataDirectory() const;

private:
    QString dataDirectory_;
    SettingsService settings_;
    LogManager logger_;
    ExecutionHistoryRepository historyRepository_;
};

} // namespace FilePilot
