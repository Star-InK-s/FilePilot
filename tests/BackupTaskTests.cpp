#include "BackupTaskTests.h"

#include "core/backup/BackupPlanTypes.h"
#include "core/backup/BackupTask.h"
#include "core/filesystem/FileHasher.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace FilePilot {
namespace BackupTaskTest {
namespace {

bool writeTestFile(const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(contents) == contents.size();
}

BackupPlan makePlan(
    const QString &source,
    const QString &destinationRoot,
    const QString &destination)
{
    BackupPlan plan;
    plan.sourcePath = source;
    plan.sourceKind = BackupEntryKind::File;
    plan.destinationRoot = destinationRoot;
    plan.finalDestinationPath = destination;
    plan.conflictPolicy = ConflictPolicy::Overwrite;

    BackupPlanItem item;
    item.relativePath = QFileInfo(source).fileName();
    item.sourcePath = source;
    item.plannedDestinationPath = destination;
    item.kind = BackupEntryKind::File;
    item.conflictPolicy = ConflictPolicy::Overwrite;
    item.plannedStatus = BackupPlanItemStatus::Ready;
    plan.items.push_back(item);
    return plan;
}

} // namespace

void BackupTaskContractTest::runsSingleFileAndReportsLifecycle()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString destinationRoot =
        QDir(directory.path()).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("payload")));
    const QString destination =
        QDir(destinationRoot).filePath(QStringLiteral("source.txt"));

    BackupTask task;
    QSignalSpy stateSpy(&task, &BackupTask::stateChanged);
    QSignalSpy progressSpy(&task, &BackupTask::progressChanged);
    QSignalSpy currentSpy(&task, &BackupTask::currentFileChanged);
    QSignalSpy completedSpy(&task, &BackupTask::completed);
    QSignalSpy failedSpy(&task, &BackupTask::failed);

    const BackupPlan plan = makePlan(source, destinationRoot, destination);
    QVERIFY(task.start(plan));
    QTRY_COMPARE(completedSpy.count(), 1);
    QCOMPARE(stateSpy.count() >= 2, true);
    QVERIFY(progressSpy.count() > 0);
    QVERIFY(currentSpy.count() > 0);
    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(task.state(), TaskState::Completed);
    QVERIFY(task.hasResult());
    const BackupExecutionResult result = task.result();
    QCOMPARE(result.status, BackupExecutionStatus::Succeeded);
    QVERIFY(result.published);
    QVERIFY(result.verified);
    QVERIFY(QFileInfo::exists(source));
    QVERIFY(QFileInfo::exists(destination));
}

void BackupTaskContractTest::rejectsRepeatedStartWhileActive()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString destinationRoot =
        QDir(directory.path()).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("payload")));
    const QString destination =
        QDir(destinationRoot).filePath(QStringLiteral("source.txt"));

    BackupTask task;
    const BackupPlan plan = makePlan(source, destinationRoot, destination);
    QVERIFY(task.start(plan));
    QVERIFY(!task.start(plan));
    QTRY_VERIFY(!task.isActive());
    QCOMPARE(task.state(), TaskState::Completed);
}

void BackupTaskContractTest::terminalStateIsStableAndReusable()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString destinationRoot =
        QDir(directory.path()).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("payload")));
    const QString destination =
        QDir(destinationRoot).filePath(QStringLiteral("source.txt"));

    BackupTask task;
    const BackupPlan plan = makePlan(source, destinationRoot, destination);
    QSignalSpy completedSpy(&task, &BackupTask::completed);
    QVERIFY(task.start(plan));
    QTRY_COMPARE(completedSpy.count(), 1);
    const TaskState terminal = task.state();
    const BackupExecutionResult firstResult = task.result();

    task.cancel();
    QCOMPARE(task.state(), terminal);
    QCOMPARE(task.result().status, firstResult.status);

    QVERIFY(task.start(plan));
    QTRY_COMPARE(completedSpy.count(), 2);
    QCOMPARE(task.state(), TaskState::Completed);
    QCOMPARE(task.result().status, BackupExecutionStatus::Succeeded);
}

void BackupTaskContractTest::reportsFailedFinalResultForInvalidPlan()
{
    BackupTask task;
    QSignalSpy completedSpy(&task, &BackupTask::completed);
    QSignalSpy failedSpy(&task, &BackupTask::failed);
    QVERIFY(task.start(BackupPlan{}));
    QTRY_COMPARE(completedSpy.count(), 1);
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(task.state(), TaskState::Failed);
    QVERIFY(task.hasResult());
    QCOMPARE(task.result().status, BackupExecutionStatus::Failed);
    QVERIFY(!task.result().errorMessage.isEmpty());
}

} // namespace BackupTaskTest
} // namespace FilePilot

bool shouldRunBackupTaskTestClass(const char *className)
{
    const QByteArray selected = qgetenv("FILEPILOT_BACKUP_TASK_TEST_CLASS");
    return selected.isEmpty() || selected == className;
}

template<typename TestClass>
int runBackupTaskTestClass(const char *className, int argc, char *argv[])
{
    if (!shouldRunBackupTaskTestClass(className)) {
        return 0;
    }
    TestClass test;
    return QTest::qExec(&test, argc, argv);
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    return runBackupTaskTestClass<FilePilot::BackupTaskTest::BackupTaskContractTest>(
        "BackupTaskContractTest", argc, argv);
}
