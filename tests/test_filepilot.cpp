#include "TestCases.h"

#include "app/Application.h"
#include "app/MainWindow.h"
#include "core/logging/LogManager.h"
#include "core/model/AppError.h"
#include "core/model/FileInfo.h"
#include "core/model/TaskState.h"
#include "core/settings/SettingsService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QToolBar>

namespace FilePilot {
namespace Test {

namespace {

Application &testApplication()
{
    return *static_cast<Application *>(QCoreApplication::instance());
}

} // namespace

void CoreModelTest::fileInfoValidity()
{
    FileInfo empty;
    QVERIFY(!empty.isValid());

    FileInfo file;
    file.absolutePath = QStringLiteral("C:/Data/report.pdf");
    file.fileName = QStringLiteral("report.pdf");
    file.extension = QStringLiteral("pdf");
    file.sizeBytes = 128;
    file.kind = FileKind::RegularFile;
    file.category = QStringLiteral("Documents");

    QVERIFY(file.isValid());
    QCOMPARE(file.sizeBytes, 128);

    FileInfo copy = file;
    QCOMPARE(copy, file);
}

void CoreModelTest::taskStateNames()
{
    QCOMPARE(taskStateName(TaskState::Idle), QStringLiteral("Idle"));
    QCOMPARE(taskStateName(TaskState::Running), QStringLiteral("Running"));
    QCOMPARE(taskStateName(TaskState::CompletedWithErrors),
             QStringLiteral("Completed with errors"));
    QCOMPARE(taskStateName(TaskState::Failed), QStringLiteral("Failed"));
}

void CoreModelTest::taskStateTerminality()
{
    QVERIFY(!isTerminalTaskState(TaskState::Idle));
    QVERIFY(!isTerminalTaskState(TaskState::Preparing));
    QVERIFY(!isTerminalTaskState(TaskState::Running));
    QVERIFY(!isTerminalTaskState(TaskState::Cancelling));
    QVERIFY(isTerminalTaskState(TaskState::Completed));
    QVERIFY(isTerminalTaskState(TaskState::CompletedWithErrors));
    QVERIFY(isTerminalTaskState(TaskState::Cancelled));
    QVERIFY(isTerminalTaskState(TaskState::Failed));
}

void CoreModelTest::appError()
{
    const AppError empty;
    QVERIFY(!empty.isValid());

    const AppError error(ErrorCode::AccessDenied,
                         QStringLiteral("Cannot read file"),
                         QStringLiteral("C:/Data/private.pdf"));
    QVERIFY(error.isValid());
    QCOMPARE(error.code(), ErrorCode::AccessDenied);
    QCOMPARE(error.message(), QStringLiteral("Cannot read file"));
    QCOMPARE(error.context(), QStringLiteral("C:/Data/private.pdf"));
    QCOMPARE(errorCodeName(error.code()), QStringLiteral("Access denied"));
}

void SettingsServiceTest::storesValuesInIniFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString settingsPath =
        QDir(directory.path()).filePath(QStringLiteral("settings.ini"));
    const QByteArray geometry = QByteArrayLiteral("geometry-value");
    const QByteArray state = QByteArrayLiteral("state-value");

    {
        SettingsService settings(settingsPath);
        settings.setLastDirectory(QStringLiteral("D:/Downloads"));
        settings.setDefaultBackupDirectory(QStringLiteral("E:/Backup"));
        settings.setLogLevelValue(2);
        settings.setWindowGeometry(geometry);
        settings.setWindowState(state);
        settings.sync();
    }

    SettingsService reloaded(settingsPath);
    QCOMPARE(reloaded.lastDirectory(), QStringLiteral("D:/Downloads"));
    QCOMPARE(reloaded.defaultBackupDirectory(), QStringLiteral("E:/Backup"));
    QCOMPARE(reloaded.logLevelValue(), 2);
    QCOMPARE(reloaded.windowGeometry(), geometry);
    QCOMPARE(reloaded.windowState(), state);
}

void LogManagerTest::writesEnabledEntries()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString logPath =
        QDir(directory.path()).filePath(QStringLiteral("logs/test.log"));
    {
        LogManager manager(logPath, LogLevel::Info);
        QVERIFY(manager.isAvailable());
        QVERIFY(!manager.log(LogLevel::Debug,
                            QStringLiteral("Test"),
                            QStringLiteral("Ignored debug entry")));
        QVERIFY(manager.log(LogLevel::Info,
                           QStringLiteral("Test"),
                           QStringLiteral("Info entry")));
        QVERIFY(manager.log(LogLevel::Error,
                           QStringLiteral("Test"),
                           QStringLiteral("Error entry")));
        QCOMPARE(manager.entryCount(), 2);
        manager.flush();
    }

    QFile file(logPath);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QTextStream stream(&file);
    const QString contents = stream.readAll();
    QVERIFY(contents.contains(QStringLiteral("[INFO] Test: Info entry")));
    QVERIFY(contents.contains(QStringLiteral("[ERROR] Test: Error entry")));
    QVERIFY(!contents.contains(QStringLiteral("Ignored debug entry")));
}

void MainWindowTest::buildsRequiredShell()
{
    MainWindow window(testApplication());
    QCOMPARE(window.windowTitle(), QStringLiteral("FilePilot"));
    QVERIFY(window.minimumWidth() >= 960);
    QVERIFY(window.minimumHeight() >= 640);
}

void MainWindowTest::switchesPagesThroughNavigation()
{
    MainWindow window(testApplication());

    auto *navigation = window.findChild<QListWidget *>(QStringLiteral("navigation"));
    auto *pageStack = window.findChild<QStackedWidget *>(QStringLiteral("pageStack"));
    auto *progressPanel = window.findChild<QWidget *>(QStringLiteral("globalProgressPanel"));
    auto *progressBar =
        window.findChild<QProgressBar *>(QStringLiteral("globalProgressBar"));
    auto *cancelButton =
        window.findChild<QPushButton *>(QStringLiteral("cancelTaskButton"));
    auto *toolBar = window.findChild<QToolBar *>(QStringLiteral("mainToolBar"));

    QVERIFY(navigation != nullptr);
    QVERIFY(pageStack != nullptr);
    QVERIFY(progressPanel != nullptr);
    QVERIFY(progressBar != nullptr);
    QVERIFY(cancelButton != nullptr);
    QVERIFY(toolBar != nullptr);

    QCOMPARE(navigation->count(), 5);
    QCOMPARE(pageStack->count(), 5);
    QCOMPARE(window.minimumSize(), QSize(960, 640));
    QVERIFY(!cancelButton->isEnabled());
    QCOMPARE(progressBar->value(), 0);

    const QStringList expectedPages{
        QStringLiteral("pageOrganize"),
        QStringLiteral("pageDuplicates"),
        QStringLiteral("pageBackup"),
        QStringLiteral("pageHistory"),
        QStringLiteral("pageSettings"),
    };

    for (int index = 0; index < expectedPages.size(); ++index) {
        navigation->setCurrentRow(index);
        QTRY_COMPARE(pageStack->currentWidget()->objectName(), expectedPages.at(index));
    }
}

} // namespace Test
} // namespace FilePilot

int main(int argc, char *argv[])
{
    QTemporaryDir applicationData;
    if (!applicationData.isValid()) {
        return 1;
    }

    FilePilot::Application application(argc, argv, applicationData.path());

    int status = 0;
    {
        FilePilot::Test::CoreModelTest test;
        status |= QTest::qExec(&test, argc, argv);
    }
    {
        FilePilot::Test::SettingsServiceTest test;
        status |= QTest::qExec(&test, argc, argv);
    }
    {
        FilePilot::Test::LogManagerTest test;
        status |= QTest::qExec(&test, argc, argv);
    }
    {
        FilePilot::Test::MainWindowTest test;
        status |= QTest::qExec(&test, argc, argv);
    }

    return status;
}
