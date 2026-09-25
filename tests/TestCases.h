#pragma once

#include <QObject>

namespace FilePilot {
namespace Test {

class CoreModelTest : public QObject
{
    Q_OBJECT

private slots:
    void fileInfoValidity();
    void taskStateNames();
    void taskStateTerminality();
    void appError();
};

class SettingsServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void storesValuesInIniFile();
};

class LogManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void writesEnabledEntries();
};

class MainWindowTest : public QObject
{
    Q_OBJECT

private slots:
    void buildsRequiredShell();
    void switchesPagesThroughNavigation();
};

} // namespace Test
} // namespace FilePilot
