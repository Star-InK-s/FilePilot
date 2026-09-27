#include "BackupHistoryTests.h"

#include "core/database/BackupHistoryRepository.h"

#include <QDir>
#include <QTemporaryDir>
#include <QTest>

namespace FilePilot {
namespace BackupHistoryTest {
namespace {

BackupPlan makePlan()
{
    BackupPlan plan;
    plan.operationId = QStringLiteral("backup-history-test");
    plan.sourcePath = QStringLiteral("C:/source/file.txt");
    plan.sourceKind = BackupEntryKind::File;
    plan.destinationRoot = QStringLiteral("D:/backup");
    plan.finalDestinationPath = QStringLiteral("D:/backup/file.txt");
    return plan;
}

BackupExecutionResult makeResult(
    const BackupExecutionStatus status,
    const bool published,
    const bool verified)
{
    BackupExecutionResult result;
    result.status = status;
    result.published = published;
    result.verified = verified;
    result.sourcePreserved = true;
    result.cleanupComplete = status != BackupExecutionStatus::CleanupFailed;
    result.actualDestination = QStringLiteral("D:/backup/file.txt");
    result.temporaryPath =
        QStringLiteral("D:/backup/file.txt.filepilot-backup-staging");
    result.errorMessage = status == BackupExecutionStatus::Succeeded
        ? QString()
        : QStringLiteral("test error");
    return result;
}

} // namespace

void BackupHistoryContractTest::persistsBackupResultAndSupportsLookup()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath =
        QDir(directory.path()).filePath(QStringLiteral("history.sqlite"));
    BackupHistoryRepository repository(databasePath);
    QVERIFY(repository.initialize());

    const BackupPlan plan = makePlan();
    const BackupExecutionResult result =
        makeResult(BackupExecutionStatus::CleanupFailed, true, true);
    const BackupHistoryPersistenceResult persistence =
        repository.saveBackupResult(plan, result);
    QVERIFY(persistence.persisted);
    QVERIFY(persistence.historyId > 0);
    QVERIFY(persistence.errorMessage.isEmpty());

    const auto loaded = repository.getBackup(persistence.historyId);
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->operationId, plan.operationId);
    QCOMPARE(loaded->sourcePath, plan.sourcePath);
    QCOMPARE(loaded->actualDestination, result.actualDestination);
    QCOMPARE(loaded->status, BackupExecutionStatus::CleanupFailed);
    QVERIFY(loaded->published);
    QVERIFY(loaded->verified);
    QVERIFY(loaded->sourcePreserved);
    QVERIFY(!loaded->cleanupComplete);
    QCOMPARE(repository.listBackups().size(), std::size_t{1});
    QVERIFY(repository.deleteBackup(persistence.historyId));
    QVERIFY(!repository.getBackup(persistence.historyId).has_value());
}

void BackupHistoryContractTest::historyFailureDoesNotMutateFilesystemResult()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    BackupHistoryRepository repository(directory.path());
    const BackupPlan plan = makePlan();
    const BackupExecutionResult filesystemResult =
        makeResult(BackupExecutionStatus::Succeeded, true, true);
    const BackupExecutionResult before = filesystemResult;

    const BackupHistoryPersistenceResult persistence =
        repository.saveBackupResult(plan, filesystemResult);
    QVERIFY(!persistence.persisted);
    QVERIFY(persistence.historyId == 0);
    QVERIFY(!persistence.errorMessage.isEmpty());
    QVERIFY(persistence.error.isValid());
    QCOMPARE(filesystemResult.status, before.status);
    QCOMPARE(filesystemResult.published, before.published);
    QCOMPARE(filesystemResult.verified, before.verified);
    QVERIFY(filesystemResult.errorMessage.isEmpty());
}

void BackupHistoryContractTest::preservesDistinctTerminalStatuses()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath =
        QDir(directory.path()).filePath(QStringLiteral("history.sqlite"));
    BackupHistoryRepository repository(databasePath);
    QVERIFY(repository.initialize());

    BackupPlan plan = makePlan();
    plan.operationId = QStringLiteral("cancelled");
    const BackupExecutionResult cancelled =
        makeResult(BackupExecutionStatus::Cancelled, false, false);
    const auto cancelledPersistence =
        repository.saveBackupResult(plan, cancelled);
    QVERIFY(cancelledPersistence.persisted);

    plan.operationId = QStringLiteral("verification-failed");
    const BackupExecutionResult verificationFailed =
        makeResult(BackupExecutionStatus::VerificationFailed, true, false);
    const auto verificationPersistence =
        repository.saveBackupResult(plan, verificationFailed);
    QVERIFY(verificationPersistence.persisted);

    const auto loadedCancelled =
        repository.getBackup(cancelledPersistence.historyId);
    const auto loadedVerification =
        repository.getBackup(verificationPersistence.historyId);
    QVERIFY(loadedCancelled.has_value());
    QVERIFY(loadedVerification.has_value());
    QCOMPARE(loadedCancelled->status, BackupExecutionStatus::Cancelled);
    QCOMPARE(
        loadedVerification->status,
        BackupExecutionStatus::VerificationFailed);
    QVERIFY(!loadedVerification->verified);
}

} // namespace BackupHistoryTest
} // namespace FilePilot

bool shouldRunBackupHistoryTestClass(const char *className)
{
    const QByteArray selected = qgetenv("FILEPILOT_BACKUP_HISTORY_TEST_CLASS");
    return selected.isEmpty() || selected == className;
}

template<typename TestClass>
int runBackupHistoryTestClass(const char *className, int argc, char *argv[])
{
    if (!shouldRunBackupHistoryTestClass(className)) {
        return 0;
    }
    TestClass test;
    return QTest::qExec(&test, argc, argv);
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    return runBackupHistoryTestClass<
        FilePilot::BackupHistoryTest::BackupHistoryContractTest>(
        "BackupHistoryContractTest", argc, argv);
}
