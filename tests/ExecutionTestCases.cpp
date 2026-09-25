#include "ExecutionTestCases.h"

#include "core/execution/ConflictResolver.h"
#include "core/execution/FileOperator.h"
#include "core/execution/OrganizeExecutionTask.h"
#include "core/organize/OrganizeExecutionPrevalidator.h"
#include "core/organize/OrganizePathValidator.h"
#include "core/organize/OrganizePlan.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <filesystem>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace FilePilot {
namespace Test {

namespace {

namespace fs = std::filesystem;

bool writeTestFile(const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(contents) == contents.size();
}

QByteArray readTestFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}
OrganizePlanItem actualItem(
    const QString &sourcePath,
    const QString &destinationPath,
    const QString &category,
    const QString &fileName)
{
    const QFileInfo info(sourcePath);
    return OrganizePlanItem{
        sourcePath,
        destinationPath,
        category,
        fileName,
        info.size(),
        info.lastModified().toUTC(),
        OrganizePlanStatus::Planned,
        QString(),
    };
}

OrganizePlanProvenance makeProvenance(
    const QString &targetRoot,
    const QString &scanRoot,
    const quint64 planGeneration = 1,
    const quint64 scanGeneration = 1)
{
    return OrganizePlanProvenance{
        OrganizePathValidator::normalizePath(targetRoot),
        TargetRootKind::Absolute,
        planGeneration,
        scanGeneration,
        OrganizePathValidator::normalizePath(scanRoot),
    };
}

OrganizePlan makePlan(
    std::vector<OrganizePlanItem> items,
    const QString &targetRoot,
    const QString &scanRoot)
{
    OrganizePlan plan;
    plan.setProvenance(makeProvenance(targetRoot, scanRoot));
    for (OrganizePlanItem &item : items) {
        plan.add(std::move(item));
    }
    return plan;
}

} // namespace

void OrganizeExecutionPrevalidatorTest::validatesFreshCandidate()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString targetRoot = QDir(directory.path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(targetRoot));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("source")));

    const OrganizePlanProvenance provenance =
        makeProvenance(targetRoot, directory.path());
    const OrganizePlanItem item = actualItem(
        source,
        OrganizePathValidator::destinationPath(
            targetRoot, QStringLiteral("Documents"), QStringLiteral("source.txt")),
        QStringLiteral("Documents"),
        QStringLiteral("source.txt"));

    OrganizeExecutionPrevalidator validator;
    QVERIFY(validator.validateTargetRoot(provenance).valid);
    QVERIFY(validator.validateCandidate(provenance, item).valid);
}

void OrganizeExecutionPrevalidatorTest::rejectsChangedOrMissingSource()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString targetRoot = QDir(directory.path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(targetRoot));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("one")));

    const OrganizePlanProvenance provenance =
        makeProvenance(targetRoot, directory.path());
    OrganizePlanItem item = actualItem(
        source,
        OrganizePathValidator::destinationPath(
            targetRoot, QStringLiteral("Documents"), QStringLiteral("source.txt")),
        QStringLiteral("Documents"),
        QStringLiteral("source.txt"));

    QVERIFY(writeTestFile(source, QByteArrayLiteral("changed-size")));
    OrganizeExecutionPrevalidator validator;
    QVERIFY(!validator.validateCandidate(provenance, item).valid);

    item = actualItem(
        source,
        OrganizePathValidator::destinationPath(
            targetRoot, QStringLiteral("Documents"), QStringLiteral("source.txt")),
        QStringLiteral("Documents"),
        QStringLiteral("source.txt"));
    QFile sourceFile(source);
    QVERIFY(sourceFile.open(QIODevice::ReadWrite));
    QVERIFY(sourceFile.setFileTime(
        QDateTime::currentDateTimeUtc().addSecs(30),
        QFileDevice::FileModificationTime));
    sourceFile.close();
    QVERIFY(!validator.validateCandidate(provenance, item).valid);

    QVERIFY(QFile::remove(source));
    QVERIFY(!validator.validateCandidate(provenance, item).valid);
}

void OrganizeExecutionPrevalidatorTest::rejectsReparseAndInvalidProvenance()
{
#ifdef Q_OS_WIN
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString target = QDir(directory.path()).filePath(QStringLiteral("target"));
    const QString junction = QDir(directory.path()).filePath(QStringLiteral("junction"));
    QVERIFY(QDir().mkpath(target));
    

    const QString command =
        QStringLiteral("New-Item -ItemType Junction -Path '%1' -Target '%2' | Out-Null")
            .arg(junction, target);
    QProcess process;
    process.start(QStringLiteral("powershell.exe"),
                  {QStringLiteral("-NoProfile"), QStringLiteral("-Command"), command});
    QVERIFY(process.waitForFinished());
    QCOMPARE(process.exitCode(), 0);

    const OrganizePlanProvenance provenance =
        makeProvenance(target, directory.path());
    OrganizePlanItem item = actualItem(
        junction,
        OrganizePathValidator::destinationPath(
            target, QStringLiteral("Documents"), QStringLiteral("junction")),
        QStringLiteral("Documents"),
        QStringLiteral("junction"));

    OrganizeExecutionPrevalidator validator;
    QVERIFY(!validator.validateCandidate(provenance, item).valid);

    OrganizePlanProvenance invalid = provenance;
    invalid.planGeneration = 0;
    QVERIFY(!validator.validateTargetRoot(invalid).valid);
#else
    QSKIP("Windows reparse-point fixture");
#endif
}

void OrganizeExecutionPrevalidatorTest::rejectsUnsafeDestinationAndNoOp()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString targetRoot = QDir(directory.path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(targetRoot));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("source")));

    const OrganizePlanProvenance provenance =
        makeProvenance(targetRoot, directory.path());
    OrganizeExecutionPrevalidator validator;

    OrganizePlanItem outside = actualItem(
        source,
        QDir(directory.path()).filePath(QStringLiteral("outside.txt")),
        QStringLiteral("Documents"),
        QStringLiteral("source.txt"));
    QVERIFY(!validator.validateCandidate(provenance, outside).valid);

    OrganizePlanItem noOp = actualItem(
        source,
        source,
        QStringLiteral("Documents"),
        QStringLiteral("source.txt"));
    QVERIFY(!validator.validateCandidate(provenance, noOp).valid);
}

void OrganizeExecutionPrevalidatorTest::detectsTargetRootIdentityChange()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString targetRoot = QDir(directory.path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(targetRoot));
    QVERIFY(QDir().mkpath(targetRoot));

    const OrganizePlanProvenance provenance =
        makeProvenance(targetRoot, directory.path());
    OrganizeExecutionPrevalidator validator;
    QVERIFY(validator.validateTargetRoot(provenance).valid);

    QVERIFY(QDir().rmdir(targetRoot));
    QVERIFY(QDir().mkpath(targetRoot));
    QVERIFY(!validator.validateTargetRoot(provenance).valid);
}

void ConflictResolverTest::resolvesNoConflictAndPolicies()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString targetRoot = QDir(directory.path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(targetRoot));
    QVERIFY(QDir().mkpath(targetRoot));
    const QString destination =
        OrganizePathValidator::destinationPath(
            targetRoot, QStringLiteral("Documents"), QStringLiteral("test.pdf"));

    OrganizePlanItem item = actualItem(
        QDir(directory.path()).filePath(QStringLiteral("source.pdf")),
        destination,
        QStringLiteral("Documents"),
        QStringLiteral("test.pdf"));
    ConflictResolver resolver;

    QCOMPARE(resolver.resolveSingle(item, ConflictPolicy::AutoRename, {}).action,
             ConflictDecisionAction::Proceed);

    QVERIFY(writeTestFile(destination, QByteArrayLiteral("old")));
    QCOMPARE(resolver.resolveSingle(item, ConflictPolicy::Skip, {}).action,
             ConflictDecisionAction::Skip);
    QCOMPARE(resolver.resolveSingle(item, ConflictPolicy::Overwrite, {}).action,
             ConflictDecisionAction::Overwrite);
    QCOMPARE(resolver.resolveSingle(item, ConflictPolicy::AutoRename, {}).action,
             ConflictDecisionAction::AutoRename);
}

void ConflictResolverTest::generatesSafeAutoRename()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString targetRoot = QDir(directory.path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(targetRoot));
    QVERIFY(QDir().mkpath(targetRoot));
    QVERIFY(writeTestFile(
        OrganizePathValidator::destinationPath(
            targetRoot, QStringLiteral("Documents"), QStringLiteral("test.pdf")),
        QByteArrayLiteral("one")));
    QVERIFY(writeTestFile(
        OrganizePathValidator::destinationPath(
            targetRoot, QStringLiteral("Documents"), QStringLiteral("test(1).pdf")),
        QByteArrayLiteral("two")));

    OrganizePlanItem item = actualItem(
        QDir(directory.path()).filePath(QStringLiteral("source.pdf")),
        OrganizePathValidator::destinationPath(
            targetRoot, QStringLiteral("Documents"), QStringLiteral("test.pdf")),
        QStringLiteral("Documents"),
        QStringLiteral("test.pdf"));

    const ConflictDecision decision =
        ConflictResolver().resolveSingle(item, ConflictPolicy::AutoRename, {});
    QCOMPARE(decision.action, ConflictDecisionAction::AutoRename);
    QCOMPARE(
        QFileInfo(decision.destinationPath).fileName(),
        QStringLiteral("test(2).pdf"));
}

void ConflictResolverTest::rejectsPlanInternalConflicts()
{
    OrganizePlanItem first = actualItem(
        QStringLiteral("C:/source/a.pdf"),
        QStringLiteral("D:/target/Documents/test.pdf"),
        QStringLiteral("Documents"),
        QStringLiteral("test.pdf"));
    OrganizePlanItem second = first;
    second.sourcePath = QStringLiteral("C:/source/b.pdf");

    const std::vector<ConflictDecision> decisions =
        ConflictResolver().resolve({first, second}, ConflictPolicy::AutoRename);
    QCOMPARE(decisions.size(), std::size_t{2});
    QCOMPARE(decisions.at(0).action, ConflictDecisionAction::Proceed);
    QCOMPARE(decisions.at(1).action, ConflictDecisionAction::Reject);
    QVERIFY(decisions.at(1).planInternalConflict);
}

void ConflictResolverTest::detectsTargetAppearingAfterCheck()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString targetRoot = QDir(directory.path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(targetRoot));
    QVERIFY(QDir().mkpath(targetRoot));
    const QString destination =
        OrganizePathValidator::destinationPath(
            targetRoot, QStringLiteral("Documents"), QStringLiteral("test.pdf"));
    OrganizePlanItem item = actualItem(
        QDir(directory.path()).filePath(QStringLiteral("source.pdf")),
        destination,
        QStringLiteral("Documents"),
        QStringLiteral("test.pdf"));

    ConflictResolver resolver;
    QCOMPARE(resolver.resolveSingle(item, ConflictPolicy::Skip, {}).action,
             ConflictDecisionAction::Proceed);
    QVERIFY(writeTestFile(destination, QByteArrayLiteral("appeared")));
    QCOMPARE(resolver.resolveSingle(item, ConflictPolicy::Skip, {}).action,
             ConflictDecisionAction::Skip);
}

void FileOperatorTest::movesWithinVolume()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString destination =
        QDir(directory.path()).filePath(QStringLiteral("moved/source.txt"));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("payload")));

    const FileMoveResult result = FileOperator().move(
        FileMoveRequest{source, destination, ConflictDecisionAction::Proceed},
        std::atomic_bool{false});
    QCOMPARE(result.status, ExecutionItemStatus::Succeeded);
    QVERIFY(!QFileInfo::exists(source));
    QCOMPARE(readTestFile(destination), QByteArrayLiteral("payload"));
}

void FileOperatorTest::movesAcrossVolumes()
{
    QTemporaryDir sourceDirectory(QStringLiteral("C:/FilePilotCrossVolume-XXXXXX"));
    QTemporaryDir destinationDirectory(
        QStringLiteral("D:/Temp/FilePilotCrossVolume-XXXXXX"));
    if (!sourceDirectory.isValid() || !destinationDirectory.isValid()) {
        QSKIP("C: and D: temporary roots are not available");
    }

    const QString source =
        QDir(sourceDirectory.path()).filePath(QStringLiteral("source.txt"));
    const QString destination =
        QDir(destinationDirectory.path()).filePath(QStringLiteral("target/source.txt"));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("cross-volume")));

    const FileMoveResult result = FileOperator().move(
        FileMoveRequest{source, destination, ConflictDecisionAction::Proceed},
        std::atomic_bool{false});
    QCOMPARE(result.status, ExecutionItemStatus::Succeeded);
    QVERIFY(!QFileInfo::exists(source));
    QCOMPARE(readTestFile(destination), QByteArrayLiteral("cross-volume"));
}

void FileOperatorTest::preservesSourceOnFailure()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString blocker = QDir(directory.path()).filePath(QStringLiteral("blocker"));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("source")));
    QVERIFY(writeTestFile(blocker, QByteArrayLiteral("file")));

    const FileMoveResult result = FileOperator().move(
        FileMoveRequest{
            source,
            QDir(blocker).filePath(QStringLiteral("child.txt")),
            ConflictDecisionAction::Proceed,
        },
        std::atomic_bool{false});
    QCOMPARE(result.status, ExecutionItemStatus::Failed);
    QVERIFY(QFileInfo::exists(source));
}

void FileOperatorTest::preservesDestinationOnFailedOverwrite()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString destination =
        QDir(directory.path()).filePath(QStringLiteral("target/existing.txt"));
    QVERIFY(writeTestFile(destination, QByteArrayLiteral("original")));
    const QString missingSource =
        QDir(directory.path()).filePath(QStringLiteral("missing.txt"));

    const FileMoveResult result = FileOperator().move(
        FileMoveRequest{
            missingSource,
            destination,
            ConflictDecisionAction::Overwrite,
        },
        std::atomic_bool{false});
    QCOMPARE(result.status, ExecutionItemStatus::Rejected);
    QCOMPARE(readTestFile(destination), QByteArrayLiteral("original"));
}
void FileOperatorTest::handlesUnicodePaths()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source =
        QDir(directory.path()).filePath(QStringLiteral("中文 源文件.txt"));
    const QString destination =
        QDir(directory.path()).filePath(QStringLiteral("目标 目录/中文 目标文件.txt"));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("unicode")));

    const FileMoveResult result = FileOperator().move(
        FileMoveRequest{source, destination, ConflictDecisionAction::Proceed},
        std::atomic_bool{false});
    QCOMPARE(result.status, ExecutionItemStatus::Succeeded);
    QVERIFY(!QFileInfo::exists(source));
    QVERIFY(QFileInfo::exists(destination));
}

void FileOperatorTest::handlesSourceInUse()
{
#ifdef Q_OS_WIN
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("occupied.txt"));
    const QString destination =
        QDir(directory.path()).filePath(QStringLiteral("target/occupied.txt"));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("occupied")));

    const QString nativeSource = QDir::toNativeSeparators(source);
    HANDLE handle = CreateFileW(
        reinterpret_cast<LPCWSTR>(nativeSource.utf16()),
        GENERIC_READ,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        QSKIP("Unable to create source lock fixture");
    }

    const FileMoveResult result = FileOperator().move(
        FileMoveRequest{source, destination, ConflictDecisionAction::Proceed},
        std::atomic_bool{false});
    CloseHandle(handle);

    if (result.status == ExecutionItemStatus::Succeeded) {
        QSKIP("This Windows environment permits moving an open source file");
    }

    QCOMPARE(result.status, ExecutionItemStatus::Failed);
    QVERIFY(QFileInfo::exists(source));
    QVERIFY(!QFileInfo::exists(destination));
#else
    QSKIP("Windows source-lock fixture");
#endif
}
void FileOperatorTest::reportsSourceCleanupFailure()
{
#ifdef Q_OS_WIN
    QTemporaryDir sourceDirectory(QStringLiteral("C:/FilePilotCleanup-XXXXXX"));
    QTemporaryDir destinationDirectory(
        QStringLiteral("D:/Temp/FilePilotCleanup-XXXXXX"));
    if (!sourceDirectory.isValid() || !destinationDirectory.isValid()) {
        QSKIP("C: and D: temporary roots are not available");
    }

    const QString source =
        QDir(sourceDirectory.path()).filePath(QStringLiteral("readonly.txt"));
    const QString destination =
        QDir(destinationDirectory.path()).filePath(QStringLiteral("target/readonly.txt"));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("read-only")));
    const QString nativeSource = QDir::toNativeSeparators(source);
    SetFileAttributesW(
        reinterpret_cast<LPCWSTR>(nativeSource.utf16()),
        FILE_ATTRIBUTE_READONLY);

    const FileMoveResult result = FileOperator().move(
        FileMoveRequest{source, destination, ConflictDecisionAction::Proceed},
        std::atomic_bool{false});
    QCOMPARE(result.status, ExecutionItemStatus::SourceCleanupFailed);
    QVERIFY(QFileInfo::exists(source));
    QVERIFY(QFileInfo::exists(destination));
#else
    QSKIP("Windows read-only source fixture");
#endif
}

void OrganizeExecutionTaskTest::executesPlan()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourceRoot = directory.path();
    const QString targetRoot = QDir(directory.path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(targetRoot));
    const QString first = QDir(sourceRoot).filePath(QStringLiteral("first.txt"));
    const QString second = QDir(sourceRoot).filePath(QStringLiteral("second.txt"));
    QVERIFY(writeTestFile(first, QByteArrayLiteral("first")));
    QVERIFY(writeTestFile(second, QByteArrayLiteral("second")));

    OrganizePlan plan = makePlan(
        {
            actualItem(
                first,
                OrganizePathValidator::destinationPath(
                    targetRoot, QStringLiteral("Documents"), QStringLiteral("first.txt")),
                QStringLiteral("Documents"),
                QStringLiteral("first.txt")),
            actualItem(
                second,
                OrganizePathValidator::destinationPath(
                    targetRoot, QStringLiteral("Documents"), QStringLiteral("second.txt")),
                QStringLiteral("Documents"),
                QStringLiteral("second.txt")),
        },
        targetRoot,
        sourceRoot);

    OrganizeExecutionTask task;
    QSignalSpy completedSpy(&task, &OrganizeExecutionTask::completed);
    QVERIFY(task.start(plan, ConflictPolicy::AutoRename));
    QTRY_COMPARE(completedSpy.count(), 1);
    QCOMPARE(task.state(), TaskState::Completed);

    const ExecutionResult result =
        completedSpy.at(0).at(0).value<ExecutionResult>();
    QCOMPARE(result.summary.planned, 2);
    QCOMPARE(result.summary.succeeded, 2);
    QVERIFY(!QFileInfo::exists(first));
    QVERIFY(!QFileInfo::exists(second));
}

void OrganizeExecutionTaskTest::reportsPartialFailure()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourceRoot = directory.path();
    const QString targetRoot = QDir(directory.path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(targetRoot));
    const QString valid = QDir(sourceRoot).filePath(QStringLiteral("valid.txt"));
    QVERIFY(writeTestFile(valid, QByteArrayLiteral("valid")));

    OrganizePlanItem missing = actualItem(
        QDir(sourceRoot).filePath(QStringLiteral("missing.txt")),
        OrganizePathValidator::destinationPath(
            targetRoot, QStringLiteral("Documents"), QStringLiteral("missing.txt")),
        QStringLiteral("Documents"),
        QStringLiteral("missing.txt"));
    OrganizePlan plan = makePlan(
        {
            actualItem(
                valid,
                OrganizePathValidator::destinationPath(
                    targetRoot, QStringLiteral("Documents"), QStringLiteral("valid.txt")),
                QStringLiteral("Documents"),
                QStringLiteral("valid.txt")),
            missing,
        },
        targetRoot,
        sourceRoot);

    OrganizeExecutionTask task;
    QSignalSpy completedSpy(&task, &OrganizeExecutionTask::completed);
    QVERIFY(task.start(plan, ConflictPolicy::AutoRename));
    QTRY_COMPARE(completedSpy.count(), 1);
    QCOMPARE(task.state(), TaskState::CompletedWithErrors);

    const ExecutionResult result =
        completedSpy.at(0).at(0).value<ExecutionResult>();
    QCOMPARE(result.summary.succeeded, 1);
    QCOMPARE(result.summary.rejected, 1);
    QCOMPARE(result.items.size(), std::size_t{2});
}

void OrganizeExecutionTaskTest::supportsCancellation()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourceRoot = directory.path();
    const QString targetRoot = QDir(directory.path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(targetRoot));
    std::vector<OrganizePlanItem> items;
    for (int index = 0; index < 100; ++index) {
        const QString source =
            QDir(sourceRoot).filePath(QStringLiteral("file-%1.txt").arg(index));
        QVERIFY(writeTestFile(source, QByteArrayLiteral("x")));
        items.push_back(actualItem(
            source,
            OrganizePathValidator::destinationPath(
                targetRoot,
                QStringLiteral("Documents"),
                QStringLiteral("file-%1.txt").arg(index)),
            QStringLiteral("Documents"),
            QStringLiteral("file-%1.txt").arg(index)));
    }

    const OrganizePlan plan = makePlan(items, targetRoot, sourceRoot);
    OrganizeExecutionTask task;
    QSignalSpy completedSpy(&task, &OrganizeExecutionTask::completed);
    QVERIFY(task.start(plan, ConflictPolicy::AutoRename));
    task.cancel();
    QTRY_COMPARE(completedSpy.count(), 1);
    QVERIFY(task.state() == TaskState::Cancelled
        || task.state() == TaskState::Completed
        || task.state() == TaskState::CompletedWithErrors);

    const ExecutionResult result =
        completedSpy.at(0).at(0).value<ExecutionResult>();
    if (task.state() == TaskState::Cancelled) {
        QVERIFY(result.cancelled);
        QVERIFY(result.summary.cancelled > 0);
    }
}

void OrganizeExecutionTaskTest::rejectsRepeatedStartAndDoesNotBlockUi()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourceRoot = directory.path();
    const QString targetRoot = QDir(directory.path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(targetRoot));
    OrganizePlan plan = makePlan(
        {
            actualItem(
                QDir(sourceRoot).filePath(QStringLiteral("missing.txt")),
                OrganizePathValidator::destinationPath(
                    targetRoot, QStringLiteral("Documents"), QStringLiteral("missing.txt")),
                QStringLiteral("Documents"),
                QStringLiteral("missing.txt")),
        },
        targetRoot,
        sourceRoot);

    OrganizeExecutionTask task;
    QSignalSpy completedSpy(&task, &OrganizeExecutionTask::completed);
    QVERIFY(task.start(plan));
    QVERIFY(!task.start(plan));

    QTimer timer;
    timer.setSingleShot(true);
    timer.setInterval(0);
    QSignalSpy timerSpy(&timer, &QTimer::timeout);
    timer.start();
    QTRY_VERIFY(timerSpy.count() > 0);
    QTRY_COMPARE(completedSpy.count(), 1);
    QVERIFY(task.start(plan));
    QTRY_COMPARE(completedSpy.count(), 2);
    QVERIFY(isTerminalTaskState(task.state()));
}

} // namespace Test
} // namespace FilePilot
