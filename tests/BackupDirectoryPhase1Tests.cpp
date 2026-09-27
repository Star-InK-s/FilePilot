#include "BackupDirectoryPhase1Tests.h"

#include "core/backup/BackupExecutor.h"
#include "core/backup/BackupInventoryBuilder.h"
#include "core/backup/BackupPlanTypes.h"
#include "core/filesystem/FileSnapshot.h"
#include "core/model/AppError.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>

namespace FilePilot {
namespace BackupDirectoryTest {
namespace {

struct ExpectedResult {
    BackupExecutionStatus status = BackupExecutionStatus::Failed;
    bool published = false;
    bool verified = false;
    bool sourcePreserved = false;
    bool cleanupComplete = false;
};

bool writeTestFile(const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(contents) == contents.size();
}

bool buildDirectoryPlan(
    const QString &source,
    const QString &destinationRoot,
    BackupPlan &plan,
    AppError &error)
{
    BackupInventory inventory;
    if (!BackupInventoryBuilder().build(source, inventory, error)) {
        return false;
    }

    plan = BackupPlan{};
    plan.operationId = QStringLiteral("directory-phase1");
    plan.sourcePath = source;
    plan.sourceKind = BackupEntryKind::Directory;
    plan.destinationRoot = destinationRoot;
    plan.finalDestinationPath =
        QDir(destinationRoot).filePath(QFileInfo(source).fileName());
    plan.conflictPolicy = ConflictPolicy::Overwrite;
    plan.sourceRootSnapshot = inventory.sourceRootSnapshot;
    plan.items = inventory.items;
    plan.unsupportedEntries = inventory.unsupportedEntries;
    plan.expectedFileCount = inventory.expectedFileCount;
    plan.expectedDirectoryCount = inventory.expectedDirectoryCount;
    plan.expectedBytes = inventory.expectedBytes;

    for (BackupPlanItem &item : plan.items) {
        item.plannedDestinationPath =
            QDir(plan.finalDestinationPath).filePath(item.relativePath);
        item.conflictPolicy = ConflictPolicy::Overwrite;
    }
    return true;
}

void requireResult(
    const BackupExecutionResult &result,
    const ExpectedResult &expected,
    const char *scenario)
{
    QCOMPARE(result.status, expected.status);
    QCOMPARE(result.published, expected.published);
    QCOMPARE(result.verified, expected.verified);
    QCOMPARE(result.sourcePreserved, expected.sourcePreserved);
    QCOMPARE(result.cleanupComplete, expected.cleanupComplete);
    QVERIFY2(result.status == BackupExecutionStatus::Succeeded
                 || result.status == BackupExecutionStatus::Skipped
                 || result.error.isValid()
                 || !result.errorMessage.isEmpty(),
             scenario);
}

QStringList relativeEntries(const QString &root)
{
    QStringList entries;
    QDirIterator iterator(
        root,
        QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        const QString relative = QDir(root).relativeFilePath(iterator.filePath());
        entries.append(iterator.fileInfo().isDir()
                           ? relative + QLatin1Char('/')
                           : relative);
    }
    entries.sort();
    return entries;
}

void assertDestinationMatches(
    const QString &destination,
    const QByteArray &contents)
{
    QVERIFY(QFileInfo::exists(destination));
    QFile file(destination);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), contents);
}

void assertMatchingFileIsNotPresent(const QString &path)
{
    QVERIFY(!QFileInfo::exists(path));
}

void requireMatchingFile(
    const QString &source,
    const QString &destination)
{
    FileSnapshot sourceSnapshot;
    FileSnapshot destinationSnapshot;
    AppError sourceError;
    AppError destinationError;
    QVERIFY(captureFileSnapshot(source, sourceSnapshot, sourceError));
    QVERIFY(captureFileSnapshot(destination, destinationSnapshot, destinationError));
    QCOMPARE(destinationSnapshot.size, sourceSnapshot.size);
    QCOMPARE(destinationSnapshot.sha256, sourceSnapshot.sha256);
}

#ifdef Q_OS_WIN
bool createJunction(const QString &junctionPath, const QString &targetPath)
{
    QDir().mkpath(QFileInfo(junctionPath).absolutePath());
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("FILEPILOT_JUNCTION_PATH"),
                       QDir::toNativeSeparators(junctionPath));
    environment.insert(QStringLiteral("FILEPILOT_JUNCTION_TARGET"),
                       QDir::toNativeSeparators(targetPath));

    QProcess process;
    process.setProcessEnvironment(environment);
    process.start(QStringLiteral("powershell.exe"),
                  {
                      QStringLiteral("-NoProfile"),
                      QStringLiteral("-Command"),
                      QStringLiteral(
                          "$ErrorActionPreference = 'Stop'; "
                          "New-Item -ItemType Junction "
                          "-Path $env:FILEPILOT_JUNCTION_PATH "
                          "-Target $env:FILEPILOT_JUNCTION_TARGET | Out-Null"),
                  });
    return process.waitForFinished(10000)
        && process.exitStatus() == QProcess::NormalExit
        && process.exitCode() == 0
        && QFileInfo(junctionPath).isDir();
}
#endif

QString stagingPathFor(const QString &destination)
{
    return destination + QStringLiteral(".filepilot-backup-staging");
}

} // namespace

void BackupDirectoryPhase1ContractTest::nestedDirectoryTreeSucceedsAndPreservesSource()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source"));
    const QString destinationRoot =
        QDir(directory.path()).filePath(QStringLiteral("destination-root"));
    QVERIFY(QDir().mkpath(source));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(QDir().mkpath(QDir(source).filePath(QStringLiteral("empty"))));
    QVERIFY(QDir().mkpath(QDir(source).filePath(QStringLiteral("nested/deep"))));
    QVERIFY(writeTestFile(
        QDir(source).filePath(QStringLiteral("root.txt")), QByteArrayLiteral("root")));
    QVERIFY(writeTestFile(
        QDir(source).filePath(QStringLiteral("nested/first.txt")),
        QByteArrayLiteral("first")));
    QVERIFY(writeTestFile(
        QDir(source).filePath(QStringLiteral("nested/deep/second.txt")),
        QByteArrayLiteral("second-payload")));

    BackupPlan plan;
    AppError planError;
    QVERIFY(buildDirectoryPlan(source, destinationRoot, plan, planError));
    QCOMPARE(plan.items.size(), std::size_t{6});

    bool stagingObserved = false;
    BackupExecutorHooks hooks;
    hooks.afterCopy = [&](const QString &, const QString &temporaryPath, QString &) {
        stagingObserved = QFileInfo(stagingPathFor(plan.finalDestinationPath)).isDir()
            && temporaryPath.startsWith(stagingPathFor(plan.finalDestinationPath));
        return stagingObserved;
    };

    const std::atomic_bool cancelled{false};
    const BackupExecutionResult result =
        BackupExecutor().execute(plan, cancelled, hooks);
    requireResult(
        result,
        {BackupExecutionStatus::Succeeded, true, true, true, true},
        "nested directory success");
    QVERIFY(stagingObserved);

    const QString destination = plan.finalDestinationPath;
    QVERIFY(QFileInfo(destination).isDir());
    QVERIFY(!QFileInfo::exists(stagingPathFor(destination)));
    QCOMPARE(relativeEntries(destination), relativeEntries(source));
    requireMatchingFile(
        QDir(source).filePath(QStringLiteral("root.txt")),
        QDir(destination).filePath(QStringLiteral("root.txt")));
    requireMatchingFile(
        QDir(source).filePath(QStringLiteral("nested/first.txt")),
        QDir(destination).filePath(QStringLiteral("nested/first.txt")));
    requireMatchingFile(
        QDir(source).filePath(QStringLiteral("nested/deep/second.txt")),
        QDir(destination).filePath(QStringLiteral("nested/deep/second.txt")));
    QVERIFY(QFileInfo(QDir(source).filePath(QStringLiteral("empty"))).isDir());
    QVERIFY(QFileInfo(QDir(destination).filePath(QStringLiteral("empty"))).isDir());
    QVERIFY(QFileInfo(source).isDir());
}

void BackupDirectoryPhase1ContractTest::emptyDirectorySucceedsAndPreservesSource()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("empty-source"));
    const QString destinationRoot =
        QDir(directory.path()).filePath(QStringLiteral("destination-root"));
    QVERIFY(QDir().mkpath(source));
    QVERIFY(QDir().mkpath(destinationRoot));

    BackupPlan plan;
    AppError planError;
    QVERIFY(buildDirectoryPlan(source, destinationRoot, plan, planError));
    QVERIFY(plan.items.empty());

    bool stagingObserved = false;
    BackupExecutorHooks hooks;
    hooks.beforePublish = [&](const QString &, const QString &, QString &) {
        stagingObserved = QFileInfo(stagingPathFor(plan.finalDestinationPath)).isDir();
        return stagingObserved;
    };

    const std::atomic_bool cancelled{false};
    const BackupExecutionResult result =
        BackupExecutor().execute(plan, cancelled, hooks);
    requireResult(
        result,
        {BackupExecutionStatus::Succeeded, true, true, true, true},
        "empty directory success");
    QVERIFY(stagingObserved);
    QVERIFY(QFileInfo(source).isDir());
    QVERIFY(QFileInfo(plan.finalDestinationPath).isDir());
    QVERIFY(relativeEntries(plan.finalDestinationPath).isEmpty());
    QVERIFY(!QFileInfo::exists(stagingPathFor(plan.finalDestinationPath)));
}

void BackupDirectoryPhase1ContractTest::rejectsUnsupportedReparseEntry()
{
#ifdef Q_OS_WIN
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source"));
    const QString target = QDir(directory.path()).filePath(QStringLiteral("target"));
    const QString destinationRoot =
        QDir(directory.path()).filePath(QStringLiteral("destination-root"));
    QVERIFY(QDir().mkpath(source));
    QVERIFY(QDir().mkpath(target));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(writeTestFile(
        QDir(target).filePath(QStringLiteral("target.txt")), QByteArrayLiteral("target")));
    const QString junction = QDir(source).filePath(QStringLiteral("junction"));
    if (!createJunction(junction, target)) {
        QSKIP("Windows Junction fixture could not be created");
    }

    BackupPlan plan;
    AppError planError;
    QVERIFY(buildDirectoryPlan(source, destinationRoot, plan, planError));
    QCOMPARE(plan.unsupportedEntries.size(), std::size_t{1});

    const std::atomic_bool cancelled{false};
    const BackupExecutionResult result =
        BackupExecutor().execute(plan, cancelled, {});
    requireResult(
        result,
        {BackupExecutionStatus::Failed, false, false, true, true},
        "unsupported reparse entry");
    QVERIFY(QFileInfo(source).isDir());
    QVERIFY(QFileInfo(junction).isDir());
    QVERIFY(!QFileInfo::exists(plan.finalDestinationPath));
    QVERIFY(!QFileInfo::exists(stagingPathFor(plan.finalDestinationPath)));
#else
    QSKIP("Windows Junction fixture");
#endif
}

void BackupDirectoryPhase1ContractTest::enumerationFailureDoesNotPublishOrReportSuccess()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source"));
    const QString destinationRoot =
        QDir(directory.path()).filePath(QStringLiteral("destination-root"));
    QVERIFY(QDir().mkpath(QDir(source).filePath(QStringLiteral("nested"))));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(writeTestFile(
        QDir(source).filePath(QStringLiteral("nested/file.txt")),
        QByteArrayLiteral("payload")));

    BackupPlan plan;
    AppError planError;
    QVERIFY(buildDirectoryPlan(source, destinationRoot, plan, planError));

    bool enumerationRequested = false;
    BackupExecutorHooks hooks;
    hooks.beforeDirectoryEnumeration =
        [&](const QString &, QString &error) {
            enumerationRequested = true;
            error = QStringLiteral("injected directory enumeration failure");
            return false;
        };

    const std::atomic_bool cancelled{false};
    const BackupExecutionResult result =
        BackupExecutor().execute(plan, cancelled, hooks);
    requireResult(
        result,
        {BackupExecutionStatus::Failed, false, false, true, true},
        "directory enumeration failure");
    QVERIFY(enumerationRequested);
    QVERIFY(QFileInfo(source).isDir());
    QVERIFY(!QFileInfo::exists(plan.finalDestinationPath));
    QVERIFY(!QFileInfo::exists(stagingPathFor(plan.finalDestinationPath)));
}

void BackupDirectoryPhase1ContractTest::skipLeavesExistingDirectoryUnchanged()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source"));
    const QString destinationRoot = QDir(directory.path()).filePath(QStringLiteral("destination-root"));
    QVERIFY(QDir().mkpath(source));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(writeTestFile(
        QDir(source).filePath(QStringLiteral("new.txt")), QByteArrayLiteral("new")));

    BackupPlan plan;
    AppError planError;
    QVERIFY(buildDirectoryPlan(source, destinationRoot, plan, planError));
    plan.conflictPolicy = ConflictPolicy::Skip;
    for (BackupPlanItem &item : plan.items) {
        item.conflictPolicy = ConflictPolicy::Skip;
    }
    QVERIFY(QDir().mkpath(plan.finalDestinationPath));
    QVERIFY(writeTestFile(
        QDir(plan.finalDestinationPath).filePath(QStringLiteral("existing.txt")),
        QByteArrayLiteral("existing")));

    const std::atomic_bool cancelled{false};
    const BackupExecutionResult result =
        BackupExecutor().execute(plan, cancelled, {});
    requireResult(
        result,
        {BackupExecutionStatus::Skipped, false, false, true, true},
        "skip existing directory");
    assertMatchingFileIsNotPresent(
        QDir(plan.finalDestinationPath).filePath(QStringLiteral("new.txt")));
    assertDestinationMatches(
        QDir(plan.finalDestinationPath).filePath(QStringLiteral("existing.txt")),
        QByteArrayLiteral("existing"));
    QVERIFY(!QFileInfo::exists(stagingPathFor(plan.finalDestinationPath)));
}

void BackupDirectoryPhase1ContractTest::autoRenameCreatesUniqueDirectory()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source"));
    const QString destinationRoot = QDir(directory.path()).filePath(QStringLiteral("destination-root"));
    QVERIFY(QDir().mkpath(QDir(source).filePath(QStringLiteral("nested"))));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(writeTestFile(
        QDir(source).filePath(QStringLiteral("nested/file.txt")),
        QByteArrayLiteral("payload")));

    BackupPlan plan;
    AppError planError;
    QVERIFY(buildDirectoryPlan(source, destinationRoot, plan, planError));
    plan.conflictPolicy = ConflictPolicy::AutoRename;
    for (BackupPlanItem &item : plan.items) {
        item.conflictPolicy = ConflictPolicy::AutoRename;
    }

    const QString firstCandidate =
        QDir(destinationRoot).filePath(QStringLiteral("source (1)"));
    QVERIFY(QDir().mkpath(plan.finalDestinationPath));
    QVERIFY(QDir().mkpath(firstCandidate));
    QVERIFY(writeTestFile(
        QDir(plan.finalDestinationPath).filePath(QStringLiteral("existing.txt")),
        QByteArrayLiteral("existing")));
    QVERIFY(writeTestFile(
        QDir(firstCandidate).filePath(QStringLiteral("occupied.txt")),
        QByteArrayLiteral("occupied")));

    const std::atomic_bool cancelled{false};
    const BackupExecutionResult result =
        BackupExecutor().execute(plan, cancelled, {});
    requireResult(
        result,
        {BackupExecutionStatus::Succeeded, true, true, true, true},
        "auto rename directory");
    const QString expectedDestination =
        QDir(destinationRoot).filePath(QStringLiteral("source (2)"));
    QCOMPARE(result.actualDestination, expectedDestination);
    assertDestinationMatches(
        QDir(plan.finalDestinationPath).filePath(QStringLiteral("existing.txt")),
        QByteArrayLiteral("existing"));
    assertDestinationMatches(
        QDir(firstCandidate).filePath(QStringLiteral("occupied.txt")),
        QByteArrayLiteral("occupied"));
    requireMatchingFile(
        QDir(source).filePath(QStringLiteral("nested/file.txt")),
        QDir(expectedDestination).filePath(QStringLiteral("nested/file.txt")));
    QVERIFY(!QFileInfo::exists(stagingPathFor(plan.finalDestinationPath)));
    QVERIFY(!QFileInfo::exists(stagingPathFor(expectedDestination)));
}

void BackupDirectoryPhase1ContractTest::directoryOverwriteIsExplicitlyRejected()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source"));
    const QString destinationRoot = QDir(directory.path()).filePath(QStringLiteral("destination-root"));
    QVERIFY(QDir().mkpath(source));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(writeTestFile(
        QDir(source).filePath(QStringLiteral("new.txt")), QByteArrayLiteral("new")));

    BackupPlan plan;
    AppError planError;
    QVERIFY(buildDirectoryPlan(source, destinationRoot, plan, planError));
    QVERIFY(QDir().mkpath(plan.finalDestinationPath));
    QVERIFY(writeTestFile(
        QDir(plan.finalDestinationPath).filePath(QStringLiteral("existing.txt")),
        QByteArrayLiteral("existing")));

    const std::atomic_bool cancelled{false};
    const BackupExecutionResult result =
        BackupExecutor().execute(plan, cancelled, {});
    QCOMPARE(result.status, BackupExecutionStatus::Failed);
    QVERIFY(!result.published);
    QVERIFY(!result.verified);
    QVERIFY(result.sourcePreserved);
    QVERIFY(result.cleanupComplete);
    QVERIFY(result.errorMessage.contains(QStringLiteral("不支持目录覆盖")));
    assertMatchingFileIsNotPresent(
        QDir(plan.finalDestinationPath).filePath(QStringLiteral("new.txt")));
    assertDestinationMatches(
        QDir(plan.finalDestinationPath).filePath(QStringLiteral("existing.txt")),
        QByteArrayLiteral("existing"));
    QVERIFY(!QFileInfo::exists(stagingPathFor(plan.finalDestinationPath)));
}

void BackupDirectoryPhase1ContractTest::cancelDuringCopyCleansStagingTree()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source"));
    const QString destinationRoot = QDir(directory.path()).filePath(QStringLiteral("destination-root"));
    QVERIFY(QDir().mkpath(source));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(writeTestFile(
        QDir(source).filePath(QStringLiteral("file.txt")), QByteArrayLiteral("payload")));
    BackupPlan plan;
    AppError planError;
    QVERIFY(buildDirectoryPlan(source, destinationRoot, plan, planError));

    std::atomic_bool cancelled{false};
    BackupExecutorHooks hooks;
    hooks.duringCopy = [&](qint64, const QString &, const QString &, QString &) {
        cancelled.store(true);
        return true;
    };
    const BackupExecutionResult result =
        BackupExecutor().execute(plan, cancelled, hooks);
    requireResult(
        result,
        {BackupExecutionStatus::Cancelled, false, false, true, true},
        "directory cancel during copy");
    QVERIFY(QFileInfo(source).isDir());
    QVERIFY(!QFileInfo::exists(plan.finalDestinationPath));
    QVERIFY(!QFileInfo::exists(stagingPathFor(plan.finalDestinationPath)));
}

void BackupDirectoryPhase1ContractTest::cancelBeforeVerifyCleansStagingTree()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source"));
    const QString destinationRoot = QDir(directory.path()).filePath(QStringLiteral("destination-root"));
    QVERIFY(QDir().mkpath(source));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(writeTestFile(
        QDir(source).filePath(QStringLiteral("file.txt")), QByteArrayLiteral("payload")));
    BackupPlan plan;
    AppError planError;
    QVERIFY(buildDirectoryPlan(source, destinationRoot, plan, planError));

    std::atomic_bool cancelled{false};
    BackupExecutorHooks hooks;
    hooks.beforeVerify = [&](const QString &, const QString &, QString &) {
        cancelled.store(true);
        return true;
    };
    const BackupExecutionResult result =
        BackupExecutor().execute(plan, cancelled, hooks);
    requireResult(
        result,
        {BackupExecutionStatus::Cancelled, false, false, true, true},
        "directory cancel before verify");
    QVERIFY(!QFileInfo::exists(plan.finalDestinationPath));
    QVERIFY(!QFileInfo::exists(stagingPathFor(plan.finalDestinationPath)));
}

void BackupDirectoryPhase1ContractTest::cleanupFailureAfterPublishIsReported()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source"));
    const QString destinationRoot = QDir(directory.path()).filePath(QStringLiteral("destination-root"));
    QVERIFY(QDir().mkpath(source));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(writeTestFile(
        QDir(source).filePath(QStringLiteral("file.txt")), QByteArrayLiteral("payload")));
    BackupPlan plan;
    AppError planError;
    QVERIFY(buildDirectoryPlan(source, destinationRoot, plan, planError));

    BackupExecutorHooks hooks;
    hooks.cleanupTemporary = [](const QString &, QString &error) {
        error = QStringLiteral("injected directory cleanup failure");
        return false;
    };
    const std::atomic_bool cancelled{false};
    const BackupExecutionResult result =
        BackupExecutor().execute(plan, cancelled, hooks);
    requireResult(
        result,
        {BackupExecutionStatus::CleanupFailed, true, true, true, false},
        "directory cleanup failure");
    requireMatchingFile(
        QDir(source).filePath(QStringLiteral("file.txt")),
        QDir(plan.finalDestinationPath).filePath(QStringLiteral("file.txt")));
}

void BackupDirectoryPhase1ContractTest::publishFailureCleansStagingTree()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source"));
    const QString destinationRoot = QDir(directory.path()).filePath(QStringLiteral("destination-root"));
    QVERIFY(QDir().mkpath(source));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(writeTestFile(
        QDir(source).filePath(QStringLiteral("file.txt")), QByteArrayLiteral("payload")));
    BackupPlan plan;
    AppError planError;
    QVERIFY(buildDirectoryPlan(source, destinationRoot, plan, planError));

    BackupExecutorHooks hooks;
    hooks.beforePublish = [](const QString &, const QString &, QString &error) {
        error = QStringLiteral("injected directory publish failure");
        return false;
    };
    const std::atomic_bool cancelled{false};
    const BackupExecutionResult result =
        BackupExecutor().execute(plan, cancelled, hooks);
    requireResult(
        result,
        {BackupExecutionStatus::PublishFailed, false, true, true, true},
        "directory publish failure");
    QVERIFY(!QFileInfo::exists(plan.finalDestinationPath));
    QVERIFY(!QFileInfo::exists(stagingPathFor(plan.finalDestinationPath)));
}

} // namespace BackupDirectoryTest
} // namespace FilePilot

bool shouldRunBackupDirectoryTestClass(const char *className)
{
    const QByteArray selected = qgetenv("FILEPILOT_BACKUP_DIRECTORY_TEST_CLASS");
    return selected.isEmpty() || selected == className;
}

template<typename TestClass>
int runBackupDirectoryTestClass(const char *className, int argc, char *argv[])
{
    if (!shouldRunBackupDirectoryTestClass(className)) {
        return 0;
    }
    TestClass test;
    return QTest::qExec(&test, argc, argv);
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    return runBackupDirectoryTestClass<
        FilePilot::BackupDirectoryTest::BackupDirectoryPhase1ContractTest>(
        "BackupDirectoryPhase1ContractTest", argc, argv);
}
