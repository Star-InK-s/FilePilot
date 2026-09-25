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

class ScanServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void emptyDirectory();
    void scansFilesAndStatistics();
    void scansNestedUnicodeAndSpecialPaths();
    void rejectsMissingDirectory();
    void recordsFileAccessFailureWithoutStopping();
    void respectsCancellation();
};

class ScanTaskTest : public QObject
{
    Q_OBJECT

private slots:
    void runsWithoutBlocking();
    void canBeCancelled();
};

class FileTableModelTest : public QObject
{
    Q_OBJECT

private slots:
    void exposesRowsColumnsAndHeaders();
    void formatsFileRows();
};

class FileOrganizePageTest : public QObject
{
    Q_OBJECT

private slots:
    void scansAndDisplaysResults();
    void reportsInvalidDirectory();
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
