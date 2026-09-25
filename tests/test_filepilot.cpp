#include "TestCases.h"

#include "app/Application.h"
#include "app/MainWindow.h"
#include "core/logging/LogManager.h"
#include "core/model/AppError.h"
#include "core/model/FileInfo.h"
#include "core/model/TaskState.h"
#include "core/scan/ScanService.h"
#include "core/tasks/ScanTask.h"
#include "ui/models/FileTableModel.h"
#include "core/settings/SettingsService.h"

#include <QCoreApplication>

#include <QDir>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTextStream>
#include <QToolBar>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

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

namespace {

bool writeFile(const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    return file.write(contents) == contents.size();
}

#ifdef Q_OS_WIN
QString windowsLongPath(const QString &path)
{
    const QString nativePath = QDir::toNativeSeparators(path);
    return nativePath.startsWith(QStringLiteral("\\\\?\\"))
        ? nativePath
        : QStringLiteral("\\\\?\\") + nativePath;
}

bool createTrailingDotFile(const QString &path, const QByteArray &contents)
{
    const QString nativePath = windowsLongPath(path);
    HANDLE handle = CreateFileW(
        reinterpret_cast<const wchar_t *>(nativePath.utf16()),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD written = 0;
    const BOOL success = WriteFile(
        handle,
        contents.constData(),
        static_cast<DWORD>(contents.size()),
        &written,
        nullptr);
    CloseHandle(handle);

    return success != FALSE && written == static_cast<DWORD>(contents.size());
}

void removeTrailingDotFile(const QString &path)
{
    const QString nativePath = windowsLongPath(path);
    DeleteFileW(reinterpret_cast<const wchar_t *>(nativePath.utf16()));
}
#endif

} // namespace

void ScanServiceTest::emptyDirectory()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const ScanService service;
    const ScanResult result = service.scan(directory.path());

    QVERIFY(result.completed);
    QVERIFY(!result.cancelled);
    QVERIFY(result.errors.empty());
    QCOMPARE(result.statistics.fileCount, 0);
    QCOMPARE(result.statistics.totalSizeBytes, 0);
    QVERIFY(result.statistics.extensionCounts.isEmpty());
}

void ScanServiceTest::scansFilesAndStatistics()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();

    QVERIFY(writeFile(QDir(root).filePath(QStringLiteral("a.txt")), QByteArrayLiteral("12345")));
    QVERIFY(writeFile(QDir(root).filePath(QStringLiteral("image.PNG")), QByteArrayLiteral("abcd")));
    QVERIFY(writeFile(QDir(root).filePath(QStringLiteral("empty.bin")), QByteArray()));
    QVERIFY(writeFile(
        QDir(root).filePath(QStringLiteral("level1/level2/report.pdf")),
        QByteArrayLiteral("xyz")));

    const ScanService service;
    const ScanResult result = service.scan(root);

    QVERIFY(result.completed);
    QCOMPARE(result.statistics.fileCount, 4);
    QCOMPARE(result.statistics.totalSizeBytes, 12);
    QCOMPARE(result.statistics.extensionCounts.value(QStringLiteral("txt")), 1);
    QCOMPARE(result.statistics.extensionCounts.value(QStringLiteral("png")), 1);
    QCOMPARE(result.statistics.extensionCounts.value(QStringLiteral("bin")), 1);
    QCOMPARE(result.statistics.extensionCounts.value(QStringLiteral("pdf")), 1);
    QCOMPARE(result.files.size(), std::size_t{4});
}

void ScanServiceTest::scansNestedUnicodeAndSpecialPaths()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();
    const QString nested = QDir(root).filePath(QStringLiteral("中文 目录 [测试] & #"));

    QVERIFY(writeFile(
        QDir(nested).filePath(QStringLiteral("报告 (最终).txt")),
        QByteArrayLiteral("中文内容")));
    QVERIFY(writeFile(
        QDir(nested).filePath(QStringLiteral("特殊 #[]()&%.bin")),
        QByteArrayLiteral("x")));

    const ScanService service;
    const ScanResult result = service.scan(root);

    QVERIFY(result.completed);
    QCOMPARE(result.statistics.fileCount, 2);
    QCOMPARE(result.statistics.totalSizeBytes, 13);
    QCOMPARE(result.statistics.errorCount, 0);

    bool foundChineseFile = false;
    bool foundSpecialFile = false;
    for (const FileInfo &file : result.files) {
        foundChineseFile |=
            file.fileName == QStringLiteral("报告 (最终).txt");
        foundSpecialFile |=
            file.fileName == QStringLiteral("特殊 #[]()&%.bin");
    }
    QVERIFY(foundChineseFile);
    QVERIFY(foundSpecialFile);
}

void ScanServiceTest::rejectsMissingDirectory()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString missing =
        QDir(directory.path()).filePath(QStringLiteral("does-not-exist"));

    const ScanService service;
    const ScanResult result = service.scan(missing);

    QVERIFY(!result.completed);
    QVERIFY(!result.cancelled);
    QCOMPARE(result.statistics.fileCount, 0);
    QCOMPARE(result.statistics.errorCount, 1);
    QVERIFY(!result.fatalError.isEmpty());
}

void ScanServiceTest::recordsFileAccessFailureWithoutStopping()
{
#ifdef Q_OS_WIN
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();

    QVERIFY(writeFile(
        QDir(root).filePath(QStringLiteral("good.txt")), QByteArrayLiteral("ok")));
    const QString badPath = QDir(root).filePath(QStringLiteral("unreadable."));
    QVERIFY(createTrailingDotFile(badPath, QByteArrayLiteral("bad")));

    const ScanService service;
    bool errorReported = false;
    const ScanResult result = service.scan(
        root,
        ScanCancellationToken{},
        {},
        [&errorReported](const ScanError &) { errorReported = true; });

    QVERIFY(result.completed);
    QVERIFY(errorReported);
    QVERIFY(result.statistics.errorCount >= 1);
    QVERIFY(!result.errors.empty());
    QCOMPARE(result.statistics.extensionCounts.value(QStringLiteral("txt")), 1);

    removeTrailingDotFile(badPath);
#else
    QSKIP("Windows-specific file access failure fixture");
#endif
}

void ScanServiceTest::respectsCancellation()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    ScanCancellationToken token;
    token.cancel();

    const ScanService service;
    const ScanResult result = service.scan(directory.path(), token);

    QVERIFY(result.cancelled);
    QVERIFY(!result.completed);
    QCOMPARE(result.statistics.fileCount, 0);
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

void ScanTaskTest::runsWithoutBlocking()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();

    for (int index = 0; index < 1000; ++index) {
        QVERIFY(writeFile(
            QDir(root).filePath(QStringLiteral("file-%1.txt").arg(index)),
            QByteArrayLiteral("x")));
    }

    ScanTask task;
    QSignalSpy completedSpy(&task, &ScanTask::completed);

    QTimer timer;
    timer.setSingleShot(true);
    timer.setInterval(0);
    QSignalSpy timerSpy(&timer, &QTimer::timeout);
    timer.start();

    QVERIFY(task.start(root));
    QTRY_VERIFY(timerSpy.count() > 0);
    QTRY_COMPARE(completedSpy.count(), 1);
    QCOMPARE(task.state(), TaskState::Completed);
    QVERIFY(!task.isActive());
}

void ScanTaskTest::canBeCancelled()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();

    for (int index = 0; index < 2000; ++index) {
        QVERIFY(writeFile(
            QDir(root).filePath(QStringLiteral("cancel-%1.txt").arg(index)),
            QByteArrayLiteral("x")));
    }

    ScanTask task;
    QSignalSpy cancelledSpy(&task, &ScanTask::cancelled);
    connect(&task, &ScanTask::progressChanged, &task, [&task] {
        task.cancel();
    });

    QVERIFY(task.start(root));
    QTRY_COMPARE(cancelledSpy.count(), 1);
    QCOMPARE(task.state(), TaskState::Cancelled);
    QVERIFY(!task.isActive());
}
void FileTableModelTest::exposesRowsColumnsAndHeaders()
{
    FileTableModel model;
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.columnCount(), 5);
    QCOMPARE(model.headerData(0, Qt::Horizontal).toString(), QStringLiteral("文件名"));
    QCOMPARE(model.headerData(1, Qt::Horizontal).toString(), QStringLiteral("类型"));
    QCOMPARE(model.headerData(2, Qt::Horizontal).toString(), QStringLiteral("大小"));
    QCOMPARE(model.headerData(3, Qt::Horizontal).toString(), QStringLiteral("修改时间"));
    QCOMPARE(model.headerData(4, Qt::Horizontal).toString(), QStringLiteral("路径"));
}

void FileTableModelTest::formatsFileRows()
{
    FileInfo first;
    first.absolutePath = QStringLiteral("C:/Data/report.pdf");
    first.fileName = QStringLiteral("report.pdf");
    first.extension = QStringLiteral("pdf");
    first.sizeBytes = 1536;
    first.modifiedUtc = QDateTime::fromMSecsSinceEpoch(0, QTimeZone::UTC);

    FileInfo second;
    second.absolutePath = QStringLiteral("C:/Data/notes");
    second.fileName = QStringLiteral("notes");
    second.extension = QString();
    second.sizeBytes = 0;

    FileTableModel model;
    model.setFiles({first, second});

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0, 0)).toString(), QStringLiteral("report.pdf"));
    QCOMPARE(model.data(model.index(0, 1)).toString(), QStringLiteral("PDF 文件"));
    QCOMPARE(model.data(model.index(0, 2)).toString(), QStringLiteral("1.5 KB"));
    QCOMPARE(model.data(model.index(1, 1)).toString(), QStringLiteral("文件"));
    QCOMPARE(model.data(model.index(1, 4)).toString(), QStringLiteral("C:/Data/notes"));
    QCOMPARE(model.data(model.index(0, 4), Qt::ToolTipRole).toString(),
             QStringLiteral("C:/Data/report.pdf"));
    QCOMPARE(FileTableModel::formatFileSize(0), QStringLiteral("0 B"));
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
        FilePilot::Test::ScanServiceTest test;
        status |= QTest::qExec(&test, argc, argv);
    }
    {
        FilePilot::Test::ScanTaskTest test;
        status |= QTest::qExec(&test, argc, argv);
    }
    {
        FilePilot::Test::FileTableModelTest test;
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

