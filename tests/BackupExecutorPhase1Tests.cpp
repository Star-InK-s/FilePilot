#include "BackupExecutorPhase1Tests.h"

#include "core/backup/BackupExecutor.h"
#include "core/backup/BackupPlanTypes.h"
#include "core/filesystem/FileHasher.h"
#include "core/filesystem/FileSnapshot.h"
#include "core/model/AppError.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>

namespace FilePilot {
namespace BackupExecutorTest {

namespace {

using ExpectedStatus = BackupExecutionStatus;
using BackupExecutorHooks = FilePilot::BackupExecutorHooks;

struct ExpectedResult {
    ExpectedStatus status = ExpectedStatus::Failed;
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

BackupPlan makeSingleFilePlan(
    const QString &source,
    const QString &destinationRoot,
    const QString &destinationFile)
{
    BackupPlan plan;
    plan.sourcePath = source;
    plan.sourceKind = BackupEntryKind::File;
    plan.destinationRoot = destinationRoot;
    plan.finalDestinationPath = destinationFile;
    plan.conflictPolicy = ConflictPolicy::Overwrite;

    BackupPlanItem item;
    item.relativePath = QFileInfo(source).fileName();
    item.sourcePath = source;
    item.plannedDestinationPath = destinationFile;
    item.kind = BackupEntryKind::File;
    item.conflictPolicy = ConflictPolicy::Overwrite;
    item.plannedStatus = BackupPlanItemStatus::Ready;
    plan.items.push_back(item);
    return plan;
}

void requireBackupExecutor(
    const BackupPlan &plan,
    const std::atomic_bool &cancelled,
    const BackupExecutorHooks &hooks,
    const ExpectedResult &expected,
    const char *scenario)
{
    const BackupExecutionResult actual =
        BackupExecutor().execute(plan, cancelled, hooks);
    QCOMPARE(actual.status, expected.status);
    QCOMPARE(actual.published, expected.published);
    QCOMPARE(actual.verified, expected.verified);
    QCOMPARE(actual.sourcePreserved, expected.sourcePreserved);
    QCOMPARE(actual.cleanupComplete, expected.cleanupComplete);
    QVERIFY2(actual.status == ExpectedStatus::Succeeded
                 || actual.status == ExpectedStatus::Skipped
                 || actual.error.isValid()
                 || !actual.errorMessage.isEmpty(),
             scenario);
}

QString stagingPathFor(const QString &destination)
{
    return destination + QStringLiteral(".filepilot-backup-staging");
}

void assertSourcePreserved(const QString &source, const QByteArray &contents)
{
    QVERIFY(QFileInfo::exists(source));
    QFile file(source);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), contents);
}

void assertDestinationMatches(const QString &destination, const QByteArray &contents)
{
    QVERIFY(QFileInfo::exists(destination));
    QFile file(destination);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), contents);
}

} // namespace

void BackupExecutorContractTest::singleRegularFileSucceedsAndPreservesSource()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString destinationRoot = QDir(directory.path()).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destinationRoot));
    const QString destination =
        QDir(destinationRoot).filePath(QStringLiteral("source.txt"));
    const QByteArray contents = QByteArrayLiteral("backup-payload");
    QVERIFY(writeTestFile(source, contents));

    FileSnapshot snapshot;
    AppError snapshotError;
    QVERIFY(captureFileSnapshot(source, snapshot, snapshotError));
    QCOMPARE(snapshot.size, qint64(contents.size()));
    QCOMPARE(snapshot.sha256,
             QCryptographicHash::hash(contents, QCryptographicHash::Sha256));

    const BackupPlan plan = makeSingleFilePlan(source, destinationRoot, destination);
    QCOMPARE(plan.items.size(), std::size_t{1});
    QCOMPARE(plan.items.front().plannedDestinationPath, destination);
    const QString stagingPath = stagingPathFor(destination);
    QVERIFY(!QFileInfo::exists(stagingPath));

    BackupExecutorHooks hooks;
    hooks.beforeCopy = [](const QString &, const QString &, QString &) { return true; };
    hooks.afterCopy = [](const QString &, const QString &, QString &) { return true; };
    hooks.beforeVerify = [](const QString &, const QString &, QString &) { return true; };
    hooks.temporaryVerifier = [](const QString &, const QString &, QString &) { return true; };
    hooks.beforePublish = [](const QString &, const QString &, QString &) { return true; };
    hooks.afterPublish = [](const QString &, const QString &, QString &) { return true; };
    hooks.cleanupTemporary = [](const QString &, QString &) { return true; };

    const std::atomic_bool cancelled{false};
    requireBackupExecutor(
        plan,
        cancelled,
        hooks,
        {ExpectedStatus::Succeeded, true, true, true, true},
        "single regular file success");
    assertSourcePreserved(source, contents);
    assertDestinationMatches(destination, contents);
    QVERIFY(!QFileInfo::exists(stagingPath));
}

void BackupExecutorContractTest::failsWhenSourceMissing()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("missing.txt"));
    const QString destinationRoot = QDir(directory.path()).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destinationRoot));
    const QString destination = QDir(destinationRoot).filePath(QStringLiteral("missing.txt"));
    QVERIFY(!QFileInfo::exists(source));
    const BackupPlan plan = makeSingleFilePlan(source, destinationRoot, destination);
    const std::atomic_bool cancelled{false};
    requireBackupExecutor(
        plan,
        cancelled,
        {},
        {ExpectedStatus::Failed, false, false, true, true},
        "source missing");
    QVERIFY(!QFileInfo::exists(destination));
    QVERIFY(!QFileInfo::exists(stagingPathFor(destination)));
}

void BackupExecutorContractTest::failsWhenDestinationDirectoryUnavailable()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString destinationRoot = QDir(directory.path()).filePath(QStringLiteral("destination"));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("source")));
    QVERIFY(writeTestFile(destinationRoot, QByteArrayLiteral("not-a-directory")));
    QVERIFY(QFileInfo(destinationRoot).isFile());
    const QString destination = QDir(destinationRoot).filePath(QStringLiteral("source.txt"));
    const BackupPlan plan = makeSingleFilePlan(source, destinationRoot, destination);
    const std::atomic_bool cancelled{false};
    requireBackupExecutor(
        plan,
        cancelled,
        {},
        {ExpectedStatus::Failed, false, false, true, true},
        "destination directory unavailable");
    QVERIFY(QFileInfo::exists(source));
}

void BackupExecutorContractTest::failsOnCopyFailure()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString destinationRoot = QDir(directory.path()).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("source")));
    const QString destination = QDir(destinationRoot).filePath(QStringLiteral("source.txt"));
    BackupExecutorHooks hooks;
    hooks.beforeCopy = [](const QString &, const QString &, QString &error) {
        error = QStringLiteral("injected copy failure");
        return false;
    };
    const BackupPlan plan = makeSingleFilePlan(source, destinationRoot, destination);
    const std::atomic_bool cancelled{false};
    requireBackupExecutor(
        plan,
        cancelled,
        hooks,
        {ExpectedStatus::Failed, false, false, true, true},
        "copy failure");
    QVERIFY(QFileInfo::exists(source));
    QVERIFY(!QFileInfo::exists(destination));
    QVERIFY(!QFileInfo::exists(stagingPathFor(destination)));
}

void BackupExecutorContractTest::failsOnVerificationMismatch()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString destinationRoot = QDir(directory.path()).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("source")));
    const QString destination = QDir(destinationRoot).filePath(QStringLiteral("source.txt"));
    BackupExecutorHooks hooks;
    hooks.temporaryVerifier = [](const QString &, const QString &, QString &error) {
        error = QStringLiteral("injected verification mismatch");
        return false;
    };
    const BackupPlan plan = makeSingleFilePlan(source, destinationRoot, destination);
    const std::atomic_bool cancelled{false};
    requireBackupExecutor(
        plan,
        cancelled,
        hooks,
        {ExpectedStatus::VerificationFailed, false, false, true, true},
        "verification mismatch");
    QVERIFY(QFileInfo::exists(source));
    QVERIFY(!QFileInfo::exists(destination));
    QVERIFY(!QFileInfo::exists(stagingPathFor(destination)));
}

void BackupExecutorContractTest::failsWhenSourceChangedBeforePublish()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString destinationRoot = QDir(directory.path()).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("original")));
    const QString destination = QDir(destinationRoot).filePath(QStringLiteral("source.txt"));
    BackupExecutorHooks hooks;
    hooks.temporaryVerifier = [&](const QString &sourcePath, const QString &, QString &) {
        return writeTestFile(sourcePath, QByteArrayLiteral("changed"));
    };
    const BackupPlan plan = makeSingleFilePlan(source, destinationRoot, destination);
    const std::atomic_bool cancelled{false};
    requireBackupExecutor(
        plan,
        cancelled,
        hooks,
        {ExpectedStatus::SourceChanged, false, false, true, true},
        "source changed before publish");
    assertSourcePreserved(source, QByteArrayLiteral("changed"));
    QVERIFY(!QFileInfo::exists(destination));
    QVERIFY(!QFileInfo::exists(stagingPathFor(destination)));
}

void BackupExecutorContractTest::failsWhenSourceDeletedBeforePublish()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString destinationRoot = QDir(directory.path()).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("source")));
    const QString destination = QDir(destinationRoot).filePath(QStringLiteral("source.txt"));
    BackupExecutorHooks hooks;
    hooks.temporaryVerifier = [&](const QString &sourcePath, const QString &, QString &) {
        return QFile::remove(sourcePath);
    };
    const BackupPlan plan = makeSingleFilePlan(source, destinationRoot, destination);
    const std::atomic_bool cancelled{false};
    requireBackupExecutor(
        plan,
        cancelled,
        hooks,
        {ExpectedStatus::SourceChanged, false, false, false, true},
        "source deleted before publish");
    QVERIFY(!QFileInfo::exists(source));
    QVERIFY(!QFileInfo::exists(destination));
    QVERIFY(!QFileInfo::exists(stagingPathFor(destination)));
}

void BackupExecutorContractTest::rejectsDestinationMutationBeforePublish()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString destinationRoot = QDir(directory.path()).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destinationRoot));
    const QString destination = QDir(destinationRoot).filePath(QStringLiteral("source.txt"));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("source")));
    QVERIFY(writeTestFile(destination, QByteArrayLiteral("old")));
    BackupExecutorHooks hooks;
    hooks.temporaryVerifier = [&](const QString &, const QString &, QString &) {
        return writeTestFile(destination, QByteArrayLiteral("replacement"));
    };
    const BackupPlan plan = makeSingleFilePlan(source, destinationRoot, destination);
    const std::atomic_bool cancelled{false};
    requireBackupExecutor(
        plan,
        cancelled,
        hooks,
        {ExpectedStatus::DestinationConflict, false, true, true, true},
        "destination mutation before publish");
    assertSourcePreserved(source, QByteArrayLiteral("source"));
    assertDestinationMatches(destination, QByteArrayLiteral("replacement"));
    QVERIFY(!QFileInfo::exists(stagingPathFor(destination)));
}

void BackupExecutorContractTest::cancelsBeforeCopy()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString destinationRoot = QDir(directory.path()).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("source")));
    const QString destination = QDir(destinationRoot).filePath(QStringLiteral("source.txt"));
    const BackupPlan plan = makeSingleFilePlan(source, destinationRoot, destination);
    const std::atomic_bool cancelled{true};
    QVERIFY(cancelled.load());
    requireBackupExecutor(
        plan,
        cancelled,
        {},
        {ExpectedStatus::Cancelled, false, false, true, true},
        "cancel before copy");
    assertSourcePreserved(source, QByteArrayLiteral("source"));
    QVERIFY(!QFileInfo::exists(destination));
    QVERIFY(!QFileInfo::exists(stagingPathFor(destination)));
}

void BackupExecutorContractTest::cancelsBeforePublish()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString destinationRoot = QDir(directory.path()).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("source")));
    const QString destination = QDir(destinationRoot).filePath(QStringLiteral("source.txt"));
    std::atomic_bool cancelled{false};
    BackupExecutorHooks hooks;
    hooks.beforePublish = [&](const QString &, const QString &, QString &) {
        cancelled.store(true);
        return true;
    };
    QVERIFY(!cancelled.load());
    const BackupPlan plan = makeSingleFilePlan(source, destinationRoot, destination);
    requireBackupExecutor(
        plan,
        cancelled,
        hooks,
        {ExpectedStatus::Cancelled, false, true, true, true},
        "cancel before publish");
    assertSourcePreserved(source, QByteArrayLiteral("source"));
    QVERIFY(!QFileInfo::exists(destination));
    QVERIFY(!QFileInfo::exists(stagingPathFor(destination)));
}

void BackupExecutorContractTest::cancelsDuringCopyWithoutPublishing()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString destinationRoot = QDir(directory.path()).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("source")));
    const QString destination = QDir(destinationRoot).filePath(QStringLiteral("source.txt"));

    std::atomic_bool cancelled{false};
    BackupExecutorHooks hooks;
    hooks.duringCopy = [&](qint64, const QString &, const QString &, QString &) {
        cancelled.store(true);
        return true;
    };

    const BackupPlan plan = makeSingleFilePlan(source, destinationRoot, destination);
    requireBackupExecutor(
        plan,
        cancelled,
        hooks,
        {ExpectedStatus::Cancelled, false, false, true, true},
        "cancel during copy");
    assertSourcePreserved(source, QByteArrayLiteral("source"));
    QVERIFY(!QFileInfo::exists(destination));
    QVERIFY(!QFileInfo::exists(stagingPathFor(destination)));
}

void BackupExecutorContractTest::cancelsBeforeVerifyWithoutPublishing()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString destinationRoot = QDir(directory.path()).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("source")));
    const QString destination = QDir(destinationRoot).filePath(QStringLiteral("source.txt"));

    std::atomic_bool cancelled{false};
    BackupExecutorHooks hooks;
    hooks.beforeVerify = [&](const QString &, const QString &, QString &) {
        cancelled.store(true);
        return true;
    };

    const BackupPlan plan = makeSingleFilePlan(source, destinationRoot, destination);
    requireBackupExecutor(
        plan,
        cancelled,
        hooks,
        {ExpectedStatus::Cancelled, false, false, true, true},
        "cancel before verify");
    assertSourcePreserved(source, QByteArrayLiteral("source"));
    QVERIFY(!QFileInfo::exists(destination));
    QVERIFY(!QFileInfo::exists(stagingPathFor(destination)));
}

void BackupExecutorContractTest::cancelAfterPublishKeepsVerifiedBackup()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString destinationRoot = QDir(directory.path()).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("source")));
    const QString destination = QDir(destinationRoot).filePath(QStringLiteral("source.txt"));

    std::atomic_bool cancelled{false};
    BackupExecutorHooks hooks;
    hooks.afterPublish = [&](const QString &, const QString &, QString &) {
        cancelled.store(true);
        return true;
    };

    const BackupPlan plan = makeSingleFilePlan(source, destinationRoot, destination);
    requireBackupExecutor(
        plan,
        cancelled,
        hooks,
        {ExpectedStatus::Succeeded, true, true, true, true},
        "cancel after publish");
    assertSourcePreserved(source, QByteArrayLiteral("source"));
    assertDestinationMatches(destination, QByteArrayLiteral("source"));
    QVERIFY(!QFileInfo::exists(stagingPathFor(destination)));
}

void BackupExecutorContractTest::reportsTempCleanupFailure()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString destinationRoot = QDir(directory.path()).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("source")));
    const QString destination = QDir(destinationRoot).filePath(QStringLiteral("source.txt"));
    BackupExecutorHooks hooks;
    hooks.cleanupTemporary = [](const QString &, QString &error) {
        error = QStringLiteral("injected temporary cleanup failure");
        return false;
    };
    const BackupPlan plan = makeSingleFilePlan(source, destinationRoot, destination);
    const std::atomic_bool cancelled{false};
    requireBackupExecutor(
        plan,
        cancelled,
        hooks,
        {ExpectedStatus::CleanupFailed, true, true, true, false},
        "temporary cleanup failure");
    assertSourcePreserved(source, QByteArrayLiteral("source"));
    assertDestinationMatches(destination, QByteArrayLiteral("source"));
}

void BackupExecutorContractTest::failsOnPublishFailure()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString destinationRoot = QDir(directory.path()).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("source")));
    const QString destination = QDir(destinationRoot).filePath(QStringLiteral("source.txt"));
    BackupExecutorHooks hooks;
    hooks.beforePublish = [](const QString &, const QString &, QString &error) {
        error = QStringLiteral("injected publish failure");
        return false;
    };
    const BackupPlan plan = makeSingleFilePlan(source, destinationRoot, destination);
    const std::atomic_bool cancelled{false};
    requireBackupExecutor(
        plan,
        cancelled,
        hooks,
        {ExpectedStatus::PublishFailed, false, true, true, true},
        "publish failure");
    assertSourcePreserved(source, QByteArrayLiteral("source"));
    QVERIFY(!QFileInfo::exists(destination));
    QVERIFY(!QFileInfo::exists(stagingPathFor(destination)));
}

void BackupExecutorContractTest::rejectsMismatchedPlannedSource()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString destinationRoot = QDir(directory.path()).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("source")));
    const QString destination = QDir(destinationRoot).filePath(QStringLiteral("source.txt"));

    BackupPlan plan = makeSingleFilePlan(source, destinationRoot, destination);
    plan.items.front().sourcePath =
        QDir(directory.path()).filePath(QStringLiteral("different.txt"));

    bool beforeCopyCalled = false;
    BackupExecutorHooks hooks;
    hooks.beforeCopy = [&](const QString &, const QString &, QString &) {
        beforeCopyCalled = true;
        return true;
    };

    const std::atomic_bool cancelled{false};
    const BackupExecutionResult result =
        BackupExecutor().execute(plan, cancelled, hooks);
    QCOMPARE(result.status, ExpectedStatus::Failed);
    QVERIFY(!result.published);
    QVERIFY(!result.verified);
    QVERIFY(result.sourcePreserved);
    QVERIFY(result.cleanupComplete);
    QVERIFY(!beforeCopyCalled);
    QVERIFY(!QFileInfo::exists(destination));
    QVERIFY(!QFileInfo::exists(stagingPathFor(destination)));
}

void BackupExecutorContractTest::rejectsDestinationOutsideDestinationRoot()
{
    QTemporaryDir sourceDirectory;
    QVERIFY(sourceDirectory.isValid());
    const QString source =
        QDir(sourceDirectory.path()).filePath(QStringLiteral("source.txt"));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("source")));

    QTemporaryDir destinationRoot(
        QDir::current().filePath(QStringLiteral("executor-root-XXXXXX")));
    QVERIFY(destinationRoot.isValid());
    QTemporaryDir outsideRoot;
    QVERIFY(outsideRoot.isValid());
    if (QStorageInfo(destinationRoot.path()).device()
        == QStorageInfo(outsideRoot.path()).device()) {
        QSKIP("Cross-volume destination containment requires separate mounted volumes");
    }

    const QString destination =
        QDir(outsideRoot.path()).filePath(QStringLiteral("outside.txt"));
    QVERIFY(!QFileInfo::exists(destination));

    bool beforeCopyCalled = false;
    BackupExecutorHooks hooks;
    hooks.beforeCopy = [&](const QString &, const QString &, QString &) {
        beforeCopyCalled = true;
        return true;
    };

    const BackupPlan plan =
        makeSingleFilePlan(source, destinationRoot.path(), destination);
    const std::atomic_bool cancelled{false};
    const BackupExecutionResult result =
        BackupExecutor().execute(plan, cancelled, hooks);
    QCOMPARE(result.status, ExpectedStatus::Failed);
    QVERIFY(!result.published);
    QVERIFY(!result.verified);
    QVERIFY(result.sourcePreserved);
    QVERIFY(result.cleanupComplete);
    QVERIFY(!beforeCopyCalled);
    QVERIFY(!QFileInfo::exists(destination));
    QVERIFY(!QFileInfo::exists(stagingPathFor(destination)));
}

void BackupExecutorContractTest::doesNotDeleteReplacementAtStagingPath()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString destinationRoot = QDir(directory.path()).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("source")));
    const QString destination = QDir(destinationRoot).filePath(QStringLiteral("source.txt"));
    const QString stagingPath = stagingPathFor(destination);

    BackupExecutorHooks hooks;
    hooks.afterCopy = [&](const QString &, const QString &temporaryPath, QString &) {
        if (!QFile::remove(temporaryPath)) {
            return false;
        }
        return writeTestFile(temporaryPath, QByteArrayLiteral("replacement-user-file"));
    };

    const BackupPlan plan = makeSingleFilePlan(source, destinationRoot, destination);
    const std::atomic_bool cancelled{false};
    requireBackupExecutor(
        plan,
        cancelled,
        hooks,
        {ExpectedStatus::VerificationFailed, false, false, true, false},
        "staging path replacement");
    assertSourcePreserved(source, QByteArrayLiteral("source"));
    QVERIFY(!QFileInfo::exists(destination));
    assertDestinationMatches(stagingPath, QByteArrayLiteral("replacement-user-file"));
}

void BackupExecutorContractTest::skipLeavesExistingFileUnchanged()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString destinationRoot = QDir(directory.path()).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("source")));
    const QString destination = QDir(destinationRoot).filePath(QStringLiteral("source.txt"));
    QVERIFY(writeTestFile(destination, QByteArrayLiteral("existing")));

    BackupPlan plan = makeSingleFilePlan(source, destinationRoot, destination);
    plan.conflictPolicy = ConflictPolicy::Skip;
    plan.items.front().conflictPolicy = ConflictPolicy::Skip;
    const std::atomic_bool cancelled{false};
    requireBackupExecutor(
        plan,
        cancelled,
        {},
        {ExpectedStatus::Skipped, false, false, true, true},
        "skip existing file");
    assertSourcePreserved(source, QByteArrayLiteral("source"));
    assertDestinationMatches(destination, QByteArrayLiteral("existing"));
    QVERIFY(!QFileInfo::exists(stagingPathFor(destination)));
}

void BackupExecutorContractTest::autoRenameCreatesUniqueFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString destinationRoot = QDir(directory.path()).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("source")));
    const QString destination = QDir(destinationRoot).filePath(QStringLiteral("source.txt"));
    const QString firstCandidate =
        QDir(destinationRoot).filePath(QStringLiteral("source (1).txt"));
    QVERIFY(writeTestFile(destination, QByteArrayLiteral("existing")));
    QVERIFY(writeTestFile(firstCandidate, QByteArrayLiteral("occupied")));

    BackupPlan plan = makeSingleFilePlan(source, destinationRoot, destination);
    plan.conflictPolicy = ConflictPolicy::AutoRename;
    plan.items.front().conflictPolicy = ConflictPolicy::AutoRename;
    const std::atomic_bool cancelled{false};
    const BackupExecutionResult result =
        BackupExecutor().execute(plan, cancelled, {});
    QCOMPARE(result.status, ExpectedStatus::Succeeded);
    QVERIFY(result.published);
    QVERIFY(result.verified);
    QVERIFY(result.sourcePreserved);
    QVERIFY(result.cleanupComplete);
    const QString expectedDestination =
        QDir(destinationRoot).filePath(QStringLiteral("source (2).txt"));
    QCOMPARE(result.actualDestination, expectedDestination);
    assertSourcePreserved(source, QByteArrayLiteral("source"));
    assertDestinationMatches(destination, QByteArrayLiteral("existing"));
    assertDestinationMatches(firstCandidate, QByteArrayLiteral("occupied"));
    assertDestinationMatches(expectedDestination, QByteArrayLiteral("source"));
    QVERIFY(!QFileInfo::exists(stagingPathFor(destination)));
    QVERIFY(!QFileInfo::exists(stagingPathFor(expectedDestination)));
}

void BackupExecutorContractTest::autoRenameRechecksNameBeforePublish()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString destinationRoot = QDir(directory.path()).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("source")));
    const QString destination = QDir(destinationRoot).filePath(QStringLiteral("source.txt"));
    const QString firstCandidate =
        QDir(destinationRoot).filePath(QStringLiteral("source (1).txt"));
    QVERIFY(writeTestFile(destination, QByteArrayLiteral("existing")));

    BackupPlan plan = makeSingleFilePlan(source, destinationRoot, destination);
    plan.conflictPolicy = ConflictPolicy::AutoRename;
    plan.items.front().conflictPolicy = ConflictPolicy::AutoRename;

    BackupExecutorHooks hooks;
    hooks.beforePublish = [&](const QString &, const QString &, QString &) {
        return writeTestFile(firstCandidate, QByteArrayLiteral("appeared-late"));
    };

    const std::atomic_bool cancelled{false};
    const BackupExecutionResult result =
        BackupExecutor().execute(plan, cancelled, hooks);
    QCOMPARE(result.status, ExpectedStatus::Succeeded);
    const QString expectedDestination =
        QDir(destinationRoot).filePath(QStringLiteral("source (2).txt"));
    QCOMPARE(result.actualDestination, expectedDestination);
    assertDestinationMatches(destination, QByteArrayLiteral("existing"));
    assertDestinationMatches(firstCandidate, QByteArrayLiteral("appeared-late"));
    assertDestinationMatches(expectedDestination, QByteArrayLiteral("source"));
    assertSourcePreserved(source, QByteArrayLiteral("source"));
}

void BackupExecutorContractTest::overwriteReplacesExistingFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString destinationRoot = QDir(directory.path()).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("source")));
    const QString destination = QDir(destinationRoot).filePath(QStringLiteral("source.txt"));
    QVERIFY(writeTestFile(destination, QByteArrayLiteral("old")));

    const BackupPlan plan = makeSingleFilePlan(source, destinationRoot, destination);
    const std::atomic_bool cancelled{false};
    requireBackupExecutor(
        plan,
        cancelled,
        {},
        {ExpectedStatus::Succeeded, true, true, true, true},
        "overwrite existing file");
    assertSourcePreserved(source, QByteArrayLiteral("source"));
    assertDestinationMatches(destination, QByteArrayLiteral("source"));
}

void BackupExecutorContractTest::doesNotUseOrganizeMoveSemantics()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString destinationRoot = QDir(directory.path()).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("source")));
    const QString destination = QDir(destinationRoot).filePath(QStringLiteral("source.txt"));
    QVERIFY(!QFileInfo::exists(destination));
    const QString stagingPath = stagingPathFor(destination);
    QVERIFY(!QFileInfo::exists(stagingPath));
    const BackupPlan plan = makeSingleFilePlan(source, destinationRoot, destination);
    const std::atomic_bool cancelled{false};
    requireBackupExecutor(
        plan,
        cancelled,
        {},
        {ExpectedStatus::Succeeded, true, true, true, true},
        "copy semantics preserve source");
    assertSourcePreserved(source, QByteArrayLiteral("source"));
    assertDestinationMatches(destination, QByteArrayLiteral("source"));
    QVERIFY(!QFileInfo::exists(stagingPath));
}

} // namespace BackupExecutorTest
} // namespace FilePilot

bool shouldRunBackupExecutorTestClass(const char *className)
{
    const QByteArray selected = qgetenv("FILEPILOT_BACKUP_EXECUTOR_TEST_CLASS");
    return selected.isEmpty() || selected == className;
}

template<typename TestClass>
int runBackupExecutorTestClass(const char *className, int argc, char *argv[])
{
    if (!shouldRunBackupExecutorTestClass(className)) {
        return 0;
    }
    TestClass test;
    return QTest::qExec(&test, argc, argv);
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    return runBackupExecutorTestClass<FilePilot::BackupExecutorTest::BackupExecutorContractTest>(
        "BackupExecutorContractTest", argc, argv);
}
