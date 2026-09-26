#include "ExecutionTestCases.h"

#include "core/execution/ConflictResolver.h"
#include "core/filesystem/FileIdentity.h"
#include "core/execution/FileOperator.h"
#include "core/execution/OrganizeExecutionTask.h"
#include "core/organize/OrganizeExecutionPrevalidator.h"
#include "core/organize/OrganizePathValidator.h"
#include "core/organize/OrganizePlan.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QDirIterator>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <filesystem>
#include <memory>

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
    const QString &scanRoot,
    const quint64 planGeneration = 1,
    const quint64 scanGeneration = 1)
{
    OrganizePlan plan;
    plan.setProvenance(makeProvenance(
        targetRoot, scanRoot, planGeneration, scanGeneration));
    for (OrganizePlanItem &item : items) {
        plan.add(std::move(item));
    }
    return plan;
}


ExecutionContext executionContext(const OrganizePlan &plan)
{
    return ExecutionContext{
        plan.provenance().normalizedTargetRoot,
        plan.provenance().scanSourceRoot,
        plan.provenance().planGeneration,
        plan.provenance().scanGeneration,
    };
}

std::unique_ptr<QTemporaryDir> temporaryDirOnAnotherVolume(
    const QTemporaryDir &sourceDirectory)
{
    const QString sourcePrefix =
        QDir::toNativeSeparators(sourceDirectory.path()).left(3);
    for (const QStorageInfo &volume : QStorageInfo::mountedVolumes()) {
        if (!volume.isReady() || volume.isReadOnly()) {
            continue;
        }
        const QString volumeRoot =
            QDir::fromNativeSeparators(volume.rootPath());
        const QString volumePrefix =
            QDir::toNativeSeparators(volume.rootPath()).left(3);
        if (volumePrefix.compare(sourcePrefix, Qt::CaseInsensitive) == 0) {
            continue;
        }

        QString pathTemplate = volumeRoot;
        if (!pathTemplate.endsWith(QLatin1Char('/'))) {
            pathTemplate += QLatin1Char('/');
        }
        pathTemplate += QStringLiteral("FilePilotTestBuilder-XXXXXX");

        auto directory = std::make_unique<QTemporaryDir>(pathTemplate);
        if (directory->isValid()) {
            return directory;
        }
    }

    return {};
}

qint64 countRegularFiles(const QString &root)
{
    qint64 count = 0;
    QDirIterator iterator(
        root,
        QDir::Files | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        ++count;
    }
    return count;
}

#ifdef Q_OS_WIN
bool createJunction(const QString &junctionPath, const QString &targetPath)
{
    QDir().mkpath(QFileInfo(junctionPath).absolutePath());
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(
        QStringLiteral("FILEPILOT_JUNCTION_PATH"),
        QDir::toNativeSeparators(junctionPath));
    environment.insert(
        QStringLiteral("FILEPILOT_JUNCTION_TARGET"),
        QDir::toNativeSeparators(targetPath));

    QProcess process;
    process.setProcessEnvironment(environment);
    process.start(
        QStringLiteral("powershell.exe"),
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

class JunctionGuard
{
public:
    explicit JunctionGuard(QString path)
        : path_(std::move(path))
    {
    }

    ~JunctionGuard()
    {
        QDir().rmdir(path_);
    }

private:
    QString path_;
};

class ReadOnlyAttributeGuard
{
public:
    explicit ReadOnlyAttributeGuard(QString path)
        : path_(std::move(path))
    {
    }

    ~ReadOnlyAttributeGuard()
    {
        SetFileAttributesW(
            reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path_).utf16()),
            FILE_ATTRIBUTE_NORMAL);
    }

private:
    QString path_;
};
#endif


bool inspectIdentity(const QString &path, FileIdentity &identity)
{
    FilesystemPathInfo info;
    QString error;
    if (!inspectFilesystemPath(path, info, error) || !info.identity.valid) {
        return false;
    }
    identity = info.identity;
    return true;
}

#ifdef Q_OS_WIN
bool replaceDirectoryWithJunction(
    const QString &directory,
    const QString &storedDirectory,
    std::unique_ptr<JunctionGuard> &guard,
    QString &error)
{
    std::error_code renameError;
    fs::rename(
        fs::u8path(directory.toStdString()),
        fs::u8path(storedDirectory.toStdString()),
        renameError);
    if (renameError) {
        error = QStringLiteral("无法保存原目录以制造 Junction TOCTOU");
        return false;
    }
    if (!createJunction(directory, storedDirectory)) {
        error = QStringLiteral("无法创建 Junction TOCTOU fixture");
        return false;
    }

    guard = std::make_unique<JunctionGuard>(directory);
    return true;
}
#endif

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
    QVERIFY(task.start(plan, executionContext(plan), ConflictPolicy::AutoRename));
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
    QVERIFY(task.start(plan, executionContext(plan), ConflictPolicy::AutoRename));
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
    QVERIFY(task.start(plan, executionContext(plan), ConflictPolicy::AutoRename));
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
    QVERIFY(task.start(plan, executionContext(plan)));
    QVERIFY(!task.start(plan, executionContext(plan)));

    QTimer timer;
    timer.setSingleShot(true);
    timer.setInterval(0);
    QSignalSpy timerSpy(&timer, &QTimer::timeout);
    timer.start();
    QTRY_VERIFY(timerSpy.count() > 0);
    QTRY_COMPARE(completedSpy.count(), 1);
    QVERIFY(task.start(plan, executionContext(plan)));
    QTRY_COMPARE(completedSpy.count(), 2);
    QVERIFY(isTerminalTaskState(task.state()));
}


void OrganizeExecutionPrevalidatorTest::rejectsDestinationReparseReplacementAfterValidation()
{
#ifdef Q_OS_WIN
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString targetRoot = QDir(directory.path()).filePath(QStringLiteral("target"));
    const QString outsideRoot = QDir(directory.path()).filePath(QStringLiteral("outside"));
    const QString destinationParent =
        QDir(targetRoot).filePath(QStringLiteral("Documents"));
    const QString outsideDestinationParent =
        QDir(outsideRoot).filePath(QStringLiteral("Documents"));
    QVERIFY(QDir().mkpath(destinationParent));
    QVERIFY(QDir().mkpath(outsideDestinationParent));
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

    QVERIFY(QDir().rmdir(destinationParent));
    JunctionGuard junctionGuard(destinationParent);
    QVERIFY(createJunction(destinationParent, outsideDestinationParent));

    const QString outsideDestination =
        QDir(outsideDestinationParent).filePath(QStringLiteral("source.txt"));
    const FileMoveResult result = FileOperator().move(
        FileMoveRequest{
            source,
            item.destinationPath,
            ConflictDecisionAction::Proceed,
        },
        std::atomic_bool{false});

    QCOMPARE(result.status, ExecutionItemStatus::Rejected);
    QVERIFY(QFileInfo::exists(source));
    QVERIFY(!QFileInfo::exists(outsideDestination));
#else
    QSKIP("Windows destination reparse-point TOCTOU fixture");
#endif
}

void FileOperatorTest::failsClosedOnCrossVolumeTemporaryVerificationFailure()
{
    QTemporaryDir sourceDirectory(
        QDir::tempPath() + QStringLiteral("/FilePilotVerifyBeforePublish-XXXXXX"));
    QVERIFY(sourceDirectory.isValid());
    auto destinationDirectory =
        temporaryDirOnAnotherVolume(sourceDirectory);
    if (!destinationDirectory) {
        QSKIP("A writable second volume is not available");
    }

    const QString source =
        QDir(sourceDirectory.path()).filePath(QStringLiteral("source.txt"));
    const QString targetRoot =
        QDir(destinationDirectory->path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(targetRoot));
    const QString destination =
        OrganizePathValidator::destinationPath(
            targetRoot, QStringLiteral("Documents"), QStringLiteral("target.txt"));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("new-source")));
    QVERIFY(writeTestFile(destination, QByteArrayLiteral("original")));

    const OrganizePlanItem item = actualItem(
        source,
        destination,
        QStringLiteral("Documents"),
        QStringLiteral("target.txt"));
    const ConflictDecision decision =
        ConflictResolver().resolveSingle(
            item, ConflictPolicy::Overwrite, {});
    QCOMPARE(decision.action, ConflictDecisionAction::Overwrite);
    QVERIFY(decision.expectedDestinationIdentity.valid);

    const FileMoveResult result = FileOperator().move(
        FileMoveRequest{
            source,
            destination,
            decision.action,
            decision.expectedDestinationIdentity,
        },
        std::atomic_bool{false},
        FileMoveOptions{
            [](const QString &, const QString &, QString &error) {
                error = QStringLiteral("injected temporary verification failure");
                return false;
            },
        });

    QCOMPARE(result.status, ExecutionItemStatus::Failed);
    QCOMPARE(result.errorMessage,
             QStringLiteral("injected temporary verification failure"));
    QCOMPARE(readTestFile(source), QByteArrayLiteral("new-source"));
    QCOMPARE(readTestFile(destination), QByteArrayLiteral("original"));
    QCOMPARE(countRegularFiles(targetRoot), 1);
}

void FileOperatorTest::rejectsOverwriteTargetIdentityChangeAfterConflictCheck()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString targetRoot = QDir(directory.path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(targetRoot));
    const QString destination =
        OrganizePathValidator::destinationPath(
            targetRoot, QStringLiteral("Documents"), QStringLiteral("target.txt"));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("new-source")));
    QVERIFY(writeTestFile(destination, QByteArrayLiteral("original")));

    const OrganizePlanItem item = actualItem(
        source,
        destination,
        QStringLiteral("Documents"),
        QStringLiteral("target.txt"));
    const ConflictDecision decision =
        ConflictResolver().resolveSingle(
            item, ConflictPolicy::Overwrite, {});
    QCOMPARE(decision.action, ConflictDecisionAction::Overwrite);
    QVERIFY(decision.expectedDestinationIdentity.valid);
    QCOMPARE(decision.destinationPath, destination);

    QVERIFY(QFile::remove(destination));
    QVERIFY(writeTestFile(destination, QByteArrayLiteral("replacement")));

    const FileMoveResult result = FileOperator().move(
        FileMoveRequest{
            source,
            decision.destinationPath,
            decision.action,
            decision.expectedDestinationIdentity,
        },
        std::atomic_bool{false});

    QCOMPARE(result.status, ExecutionItemStatus::Rejected);
    QCOMPARE(readTestFile(source), QByteArrayLiteral("new-source"));
    QCOMPARE(readTestFile(destination), QByteArrayLiteral("replacement"));
}

void OrganizeExecutionTaskTest::sourceCleanupFailureDoesNotCreateSecondCopyOnRetry()
{
#ifdef Q_OS_WIN
    QTemporaryDir sourceDirectory(
        QDir::tempPath() + QStringLiteral("/FilePilotSourceCleanup-XXXXXX"));
    QVERIFY(sourceDirectory.isValid());
    auto destinationDirectory =
        temporaryDirOnAnotherVolume(sourceDirectory);
    if (!destinationDirectory) {
        QSKIP("A writable second volume is not available");
    }

    const QString sourceRoot = sourceDirectory.path();
    const QString targetRoot =
        QDir(destinationDirectory->path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(targetRoot));
    const QString source =
        QDir(sourceRoot).filePath(QStringLiteral("scan/readonly.txt"));
    const QString destination =
        OrganizePathValidator::destinationPath(
            targetRoot, QStringLiteral("Documents"), QStringLiteral("readonly.txt"));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("read-only")));
    ReadOnlyAttributeGuard readOnlyGuard(source);
    QVERIFY(SetFileAttributesW(
        reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(source).utf16()),
        FILE_ATTRIBUTE_READONLY));

    const OrganizePlan plan = makePlan(
        {
            actualItem(
                source,
                destination,
                QStringLiteral("Documents"),
                QStringLiteral("readonly.txt")),
        },
        targetRoot,
        QDir(sourceRoot).filePath(QStringLiteral("scan")));

    OrganizeExecutionTask task;
    QSignalSpy completedSpy(&task, &OrganizeExecutionTask::completed);
    QVERIFY(task.start(plan, executionContext(plan), ConflictPolicy::AutoRename));
    QTRY_COMPARE(completedSpy.count(), 1);
    QCOMPARE(task.state(), TaskState::CompletedWithErrors);

    const ExecutionResult first =
        completedSpy.at(0).at(0).value<ExecutionResult>();
    QCOMPARE(first.items.size(), std::size_t{1});
    QCOMPARE(first.items.at(0).status, ExecutionItemStatus::SourceCleanupFailed);
    QCOMPARE(first.items.at(0).actualDestination, destination);
    QCOMPARE(first.summary.failed, 0);
    QCOMPARE(first.summary.sourceCleanupFailed, 1);
    QVERIFY(!first.items.at(0).resumedPublishedResult);
    QVERIFY(QFileInfo::exists(source));
    QCOMPARE(readTestFile(destination), QByteArrayLiteral("read-only"));
    QCOMPARE(countRegularFiles(targetRoot), 1);

    QTRY_VERIFY(!task.isActive());
    QVERIFY(task.start(plan, executionContext(plan), ConflictPolicy::AutoRename));
    QTRY_COMPARE(completedSpy.count(), 2);
    QCOMPARE(task.state(), TaskState::CompletedWithErrors);

    const ExecutionResult second =
        completedSpy.at(1).at(0).value<ExecutionResult>();
    QCOMPARE(second.items.size(), std::size_t{1});
    QCOMPARE(second.items.at(0).status, ExecutionItemStatus::SourceCleanupFailed);
    QVERIFY(!second.items.at(0).resumedPublishedResult);
    QCOMPARE(second.summary.failed, 0);
    QCOMPARE(second.summary.sourceCleanupFailed, 1);
    QCOMPARE(countRegularFiles(targetRoot), 1);
    QCOMPARE(second.items.at(0).actualDestination, destination);
    QVERIFY(QFileInfo::exists(source));
#else
    QSKIP("Windows read-only cross-volume source fixture");
#endif
}

void OrganizeExecutionTaskTest::rejectsForeignSourceOutsideScanSourceRoot()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString scanRoot =
        QDir(directory.path()).filePath(QStringLiteral("scan"));
    const QString foreignRoot =
        QDir(directory.path()).filePath(QStringLiteral("foreign"));
    const QString otherScanRoot =
        QDir(directory.path()).filePath(QStringLiteral("other-scan"));
    const QString targetRoot =
        QDir(directory.path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(scanRoot));
    QVERIFY(QDir().mkpath(foreignRoot));
    QVERIFY(QDir().mkpath(otherScanRoot));
    QVERIFY(QDir().mkpath(targetRoot));

    const QString source =
        QDir(foreignRoot).filePath(QStringLiteral("foreign.txt"));
    const QString destination =
        OrganizePathValidator::destinationPath(
            targetRoot, QStringLiteral("Documents"), QStringLiteral("foreign.txt"));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("foreign")));

    OrganizePlan plan = makePlan(
        {
            actualItem(
                source,
                destination,
                QStringLiteral("Documents"),
                QStringLiteral("foreign.txt")),
        },
        targetRoot,
        scanRoot,
        11,
        17);

    QVERIFY(!plan.isCurrentFor(targetRoot, 12, 17, scanRoot));
    QVERIFY(!plan.isCurrentFor(targetRoot, 11, 18, scanRoot));
    QVERIFY(!plan.isCurrentFor(targetRoot, 11, 17, otherScanRoot));

    OrganizeExecutionTask task;
    const ExecutionContext validContext{
        targetRoot,
        scanRoot,
        11,
        17,
    };
    QVERIFY(!task.start(
        plan,
        ExecutionContext{targetRoot, scanRoot, 12, 17},
        ConflictPolicy::AutoRename));
    QVERIFY(!task.start(
        plan,
        ExecutionContext{targetRoot, scanRoot, 11, 18},
        ConflictPolicy::AutoRename));
    QVERIFY(!task.start(
        plan,
        ExecutionContext{targetRoot, otherScanRoot, 11, 17},
        ConflictPolicy::AutoRename));
    QVERIFY(!task.start(plan, validContext, ConflictPolicy::AutoRename));

    QVERIFY(!task.isActive());
    QCOMPARE(task.state(), TaskState::Idle);
    QVERIFY(QFileInfo::exists(source));
    QVERIFY(!QFileInfo::exists(destination));
}


void FileOperatorTest::rejectsSourceContentChangeAfterTemporaryVerification()
{
#ifdef Q_OS_WIN
    QTemporaryDir sourceDirectory(
        QDir::tempPath() + QStringLiteral("/FilePilotSourceChanged-XXXXXX"));
    QVERIFY(sourceDirectory.isValid());
    auto destinationDirectory = temporaryDirOnAnotherVolume(sourceDirectory);
    if (!destinationDirectory) {
        QSKIP("A writable second volume is not available");
    }

    const QString source =
        QDir(sourceDirectory.path()).filePath(QStringLiteral("source.txt"));
    const QString targetRoot =
        QDir(destinationDirectory->path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(targetRoot));
    const QString destination =
        OrganizePathValidator::destinationPath(
            targetRoot, QStringLiteral("Documents"), QStringLiteral("target.txt"));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("old-source")));
    QVERIFY(writeTestFile(destination, QByteArrayLiteral("original")));

    FileIdentity sourceIdentity;
    FileIdentity destinationIdentity;
    QVERIFY(inspectIdentity(source, sourceIdentity));
    QVERIFY(inspectIdentity(destination, destinationIdentity));

    bool verifierRan = false;
    FileMoveOptions options;
    options.temporaryFileVerifier =
        [&](const QString &sourcePath,
            const QString &temporaryPath,
            QString &error) {
            verifierRan = true;
            if (readTestFile(sourcePath) != QByteArrayLiteral("old-source")
                || readTestFile(temporaryPath) != QByteArrayLiteral("old-source")) {
                error = QStringLiteral("verifier fixture was not copied as expected");
                return false;
            }
            if (!writeTestFile(sourcePath, QByteArrayLiteral("new-source"))) {
                error = QStringLiteral("could not mutate source after verification");
                return false;
            }
            return true;
        };

    const FileMoveResult result = FileOperator().move(
        FileMoveRequest{
            source,
            destination,
            ConflictDecisionAction::Overwrite,
            destinationIdentity,
            sourceIdentity,
        },
        std::atomic_bool{false},
        options);

    QVERIFY(verifierRan);
    QVERIFY(QFileInfo::exists(source));
    QCOMPARE(readTestFile(destination), QByteArrayLiteral("original"));
    QCOMPARE(result.status, ExecutionItemStatus::Rejected);
    QCOMPARE(readTestFile(source), QByteArrayLiteral("new-source"));
    QCOMPARE(countRegularFiles(targetRoot), 1);
#else
    QSKIP("Windows cross-volume source mutation fixture");
#endif
}

void FileOperatorTest::rejectsStableIdentitySourceContentChangeAfterVerification()
{
#ifdef Q_OS_WIN
    QTemporaryDir sourceDirectory(
        QDir::tempPath() + QStringLiteral("/FilePilotStableIdentity-XXXXXX"));
    QVERIFY(sourceDirectory.isValid());
    auto destinationDirectory = temporaryDirOnAnotherVolume(sourceDirectory);
    if (!destinationDirectory) {
        QSKIP("A writable second volume is not available");
    }

    const QString source =
        QDir(sourceDirectory.path()).filePath(QStringLiteral("source.txt"));
    const QString targetRoot =
        QDir(destinationDirectory->path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(targetRoot));
    const QString destination =
        OrganizePathValidator::destinationPath(
            targetRoot, QStringLiteral("Documents"), QStringLiteral("target.txt"));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("same-size-a")));
    QVERIFY(writeTestFile(destination, QByteArrayLiteral("original")));

    FileIdentity sourceIdentity;
    FileIdentity destinationIdentity;
    QVERIFY(inspectIdentity(source, sourceIdentity));
    QVERIFY(inspectIdentity(destination, destinationIdentity));

    bool identityStayedStable = false;
    FileMoveOptions options;
    options.temporaryFileVerifier =
        [&](const QString &sourcePath,
            const QString &temporaryPath,
            QString &error) {
            if (readTestFile(sourcePath) != QByteArrayLiteral("same-size-a")
                || readTestFile(temporaryPath) != QByteArrayLiteral("same-size-a")) {
                error = QStringLiteral("verifier fixture was not copied as expected");
                return false;
            }

            FileIdentity beforeMutation;
            FileIdentity afterMutation;
            if (!inspectIdentity(sourcePath, beforeMutation)
                || !writeTestFile(sourcePath, QByteArrayLiteral("same-size-b"))
                || !inspectIdentity(sourcePath, afterMutation)) {
                error = QStringLiteral("could not inspect stable source identity");
                return false;
            }
            identityStayedStable =
                beforeMutation == sourceIdentity && afterMutation == sourceIdentity;
            return true;
        };

    const FileMoveResult result = FileOperator().move(
        FileMoveRequest{
            source,
            destination,
            ConflictDecisionAction::Overwrite,
            destinationIdentity,
            sourceIdentity,
        },
        std::atomic_bool{false},
        options);

    QVERIFY(identityStayedStable);
    QVERIFY(QFileInfo::exists(source));
    QCOMPARE(readTestFile(destination), QByteArrayLiteral("original"));
    QCOMPARE(result.status, ExecutionItemStatus::Rejected);
    QCOMPARE(readTestFile(source), QByteArrayLiteral("same-size-b"));
    QCOMPARE(countRegularFiles(targetRoot), 1);
#else
    QSKIP("Windows stable-identity source mutation fixture");
#endif
}

void FileOperatorTest::rejectsDestinationParentJunctionReplacementAfterVerification()
{
#ifdef Q_OS_WIN
    QTemporaryDir sourceDirectory(
        QDir::tempPath() + QStringLiteral("/FilePilotDestinationParent-XXXXXX"));
    QVERIFY(sourceDirectory.isValid());
    auto destinationDirectory = temporaryDirOnAnotherVolume(sourceDirectory);
    if (!destinationDirectory) {
        QSKIP("A writable second volume is not available");
    }

    const QString source =
        QDir(sourceDirectory.path()).filePath(QStringLiteral("source.txt"));
    const QString targetRoot =
        QDir(destinationDirectory->path()).filePath(QStringLiteral("target"));
    const QString parent = QDir(targetRoot).filePath(QStringLiteral("parent"));
    const QString child = QDir(parent).filePath(QStringLiteral("child"));
    const QString storedParent = QDir(targetRoot).filePath(QStringLiteral("parent-real"));
    QVERIFY(QDir().mkpath(child));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("source")));

    const QString destination = QDir(child).filePath(QStringLiteral("target.txt"));
    std::unique_ptr<JunctionGuard> junctionGuard;
    bool mutationRan = false;
    FileMoveOptions options;
    options.temporaryFileVerifier =
        [&](const QString &, const QString &, QString &error) {
            mutationRan = replaceDirectoryWithJunction(
                parent, storedParent, junctionGuard, error);
            return mutationRan;
        };

    const FileMoveResult result = FileOperator().move(
        FileMoveRequest{
            source,
            destination,
            ConflictDecisionAction::Proceed,
        },
        std::atomic_bool{false},
        options);

    QVERIFY(mutationRan);
    QVERIFY(QFileInfo::exists(source));
    QVERIFY(!QFileInfo::exists(destination));
    QCOMPARE(countRegularFiles(storedParent), 0);
    QCOMPARE(result.status, ExecutionItemStatus::Rejected);
#else
    QSKIP("Windows destination parent Junction TOCTOU fixture");
#endif
}

void FileOperatorTest::rejectsSourceParentJunctionReplacementAfterVerification()
{
#ifdef Q_OS_WIN
    QTemporaryDir sourceDirectory(
        QDir::tempPath() + QStringLiteral("/FilePilotSourceParent-XXXXXX"));
    QVERIFY(sourceDirectory.isValid());
    auto destinationDirectory = temporaryDirOnAnotherVolume(sourceDirectory);
    if (!destinationDirectory) {
        QSKIP("A writable second volume is not available");
    }

    const QString sourceParent =
        QDir(sourceDirectory.path()).filePath(QStringLiteral("parent"));
    const QString sourceChild = QDir(sourceParent).filePath(QStringLiteral("child"));
    const QString storedSourceParent =
        QDir(sourceDirectory.path()).filePath(QStringLiteral("parent-real"));
    QVERIFY(QDir().mkpath(sourceChild));
    const QString source = QDir(sourceChild).filePath(QStringLiteral("source.txt"));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("source")));

    const QString targetRoot =
        QDir(destinationDirectory->path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(targetRoot));
    const QString destination =
        OrganizePathValidator::destinationPath(
            targetRoot, QStringLiteral("Documents"), QStringLiteral("source.txt"));

    std::unique_ptr<JunctionGuard> junctionGuard;
    bool mutationRan = false;
    FileMoveOptions options;
    options.temporaryFileVerifier =
        [&](const QString &, const QString &, QString &error) {
            mutationRan = replaceDirectoryWithJunction(
                sourceParent, storedSourceParent, junctionGuard, error);
            return mutationRan;
        };

    const FileMoveResult result = FileOperator().move(
        FileMoveRequest{
            source,
            destination,
            ConflictDecisionAction::Proceed,
        },
        std::atomic_bool{false},
        options);

    QVERIFY(mutationRan);
    QVERIFY(QFileInfo::exists(source));
    QVERIFY(!QFileInfo::exists(destination));
    QCOMPARE(result.status, ExecutionItemStatus::Rejected);
#else
    QSKIP("Windows source parent Junction TOCTOU fixture");
#endif
}

void FileOperatorTest::requiresTestSeamBetweenFinalIdentityCheckAndReplace()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source =
        QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString targetRoot =
        QDir(directory.path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(targetRoot));
    const QString destination =
        OrganizePathValidator::destinationPath(
            targetRoot, QStringLiteral("Documents"), QStringLiteral("target.txt"));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("new-source")));
    QVERIFY(writeTestFile(destination, QByteArrayLiteral("original")));

    const OrganizePlanItem item = actualItem(
        source,
        destination,
        QStringLiteral("Documents"),
        QStringLiteral("target.txt"));
    const ConflictDecision decision =
        ConflictResolver().resolveSingle(
            item, ConflictPolicy::Overwrite, {});
    QCOMPARE(decision.action, ConflictDecisionAction::Overwrite);
    QVERIFY(decision.expectedDestinationIdentity.valid);

    bool hookRan = false;
    FileMoveOptions options;
    options.beforePublish =
        [&](const QString &, const QString &destinationPath, QString &error) {
            hookRan = true;
            if (!QFile::remove(destinationPath)
                || !writeTestFile(destinationPath, QByteArrayLiteral("replacement"))) {
                error = QStringLiteral("could not replace target in publish seam");
                return false;
            }
            return true;
        };

    const FileMoveResult result = FileOperator().move(
        FileMoveRequest{
            source,
            decision.destinationPath,
            decision.action,
            decision.expectedDestinationIdentity,
        },
        std::atomic_bool{false},
        options);

    QVERIFY(hookRan);
    QCOMPARE(result.status, ExecutionItemStatus::Rejected);
    QCOMPARE(readTestFile(source), QByteArrayLiteral("new-source"));
    QCOMPARE(readTestFile(destination), QByteArrayLiteral("replacement"));
}

void OrganizeExecutionTaskTest::doesNotReuseRecoveryStateAcrossExecutionContexts()
{
#ifdef Q_OS_WIN
    QTemporaryDir sourceDirectory(
        QDir::tempPath() + QStringLiteral("/FilePilotRecoveryScope-XXXXXX"));
    QVERIFY(sourceDirectory.isValid());
    auto destinationDirectory = temporaryDirOnAnotherVolume(sourceDirectory);
    if (!destinationDirectory) {
        QSKIP("A writable second volume is not available");
    }

    const QString sourceRoot = sourceDirectory.path();
    const QString scanRoot = QDir(sourceRoot).filePath(QStringLiteral("scan"));
    const QString targetRoot =
        QDir(destinationDirectory->path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(scanRoot));
    QVERIFY(QDir().mkpath(targetRoot));

    const QString source = QDir(scanRoot).filePath(QStringLiteral("readonly.txt"));
    const QString destination =
        OrganizePathValidator::destinationPath(
            targetRoot, QStringLiteral("Documents"), QStringLiteral("readonly.txt"));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("read-only")));
    ReadOnlyAttributeGuard readOnlyGuard(source);
    QVERIFY(SetFileAttributesW(
        reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(source).utf16()),
        FILE_ATTRIBUTE_READONLY));

    const OrganizePlan firstPlan = makePlan(
        {
            actualItem(
                source,
                destination,
                QStringLiteral("Documents"),
                QStringLiteral("readonly.txt")),
        },
        targetRoot,
        scanRoot,
        1,
        1);

    OrganizeExecutionTask task;
    QSignalSpy completedSpy(&task, &OrganizeExecutionTask::completed);
    QVERIFY(task.start(firstPlan, executionContext(firstPlan), ConflictPolicy::AutoRename));
    QTRY_COMPARE(completedSpy.count(), 1);

    const ExecutionResult first =
        completedSpy.at(0).at(0).value<ExecutionResult>();
    QCOMPARE(first.items.size(), std::size_t{1});
    QCOMPARE(first.items.at(0).status, ExecutionItemStatus::SourceCleanupFailed);
    QVERIFY(!first.items.at(0).resumedPublishedResult);
    QVERIFY(QFileInfo::exists(source));
    QCOMPARE(countRegularFiles(targetRoot), 1);

    QVERIFY(SetFileAttributesW(
        reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(source).utf16()),
        FILE_ATTRIBUTE_NORMAL));
    QTRY_VERIFY(!task.isActive());

    const OrganizePlan secondPlan = makePlan(
        {
            actualItem(
                source,
                destination,
                QStringLiteral("Documents"),
                QStringLiteral("readonly.txt")),
        },
        targetRoot,
        scanRoot,
        2,
        2);
    QVERIFY(task.start(secondPlan, executionContext(secondPlan), ConflictPolicy::Skip));
    QTRY_COMPARE(completedSpy.count(), 2);

    const ExecutionResult second =
        completedSpy.at(1).at(0).value<ExecutionResult>();
    QCOMPARE(second.items.size(), std::size_t{1});
    QVERIFY(QFileInfo::exists(source));
    QVERIFY(!second.items.at(0).resumedPublishedResult);
    QCOMPARE(countRegularFiles(targetRoot), 1);
    QCOMPARE(second.items.at(0).status, ExecutionItemStatus::Skipped);
#else
    QSKIP("Windows cross-volume recovery-state fixture");
#endif
}


void FileOperatorTest::cancelsBeforePublishWithoutFilesystemSideEffects()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source =
        QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString targetRoot =
        QDir(directory.path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(targetRoot));
    const QString destination =
        OrganizePathValidator::destinationPath(
            targetRoot, QStringLiteral("Documents"), QStringLiteral("target.txt"));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("source")));

    std::atomic_bool cancelled{false};
    bool hookRan = false;
    FileMoveOptions options;
    options.beforePublish =
        [&](const QString &, const QString &, QString &) {
            hookRan = true;
            cancelled.store(true, std::memory_order_relaxed);
            return true;
        };

    const FileMoveResult result = FileOperator().move(
        FileMoveRequest{
            source,
            destination,
            ConflictDecisionAction::Proceed,
        },
        cancelled,
        options);

    QVERIFY(hookRan);
    QVERIFY(cancelled.load(std::memory_order_relaxed));
    QCOMPARE(result.status, ExecutionItemStatus::Cancelled);
    QCOMPARE(readTestFile(source), QByteArrayLiteral("source"));
    QVERIFY(!QFileInfo::exists(destination));
    QCOMPARE(countRegularFiles(targetRoot), 0);
}

void FileOperatorTest::requiresAtomicFileSnapshotSeam()
{
    QSKIP(
        "captureFileSnapshot() reads identity/size and digest separately; "
        "a deterministic hook inside snapshot capture is required");
}

void FileOperatorTest::requiresEnsureParentDirectoryCheckToMkdirSeam()
{
    QSKIP(
        "ensureParentDirectory() has no deterministic hook between ancestor "
        "inspection and QDir().mkdir()");
}

void FileOperatorTest::resumePublishedCleanupRejectsMismatchWithoutResumedFlag()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source =
        QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString destination =
        QDir(directory.path()).filePath(QStringLiteral("destination.txt"));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("source")));
    QVERIFY(writeTestFile(destination, QByteArrayLiteral("destination")));

    PublishedMoveRecoveryState invalidRecoveryState;
    const FileMoveResult result = FileOperator().resumePublishedCleanup(
        FileMoveRequest{
            source,
            destination,
            ConflictDecisionAction::Proceed,
        },
        invalidRecoveryState,
        std::atomic_bool{false});

    QCOMPARE(result.status, ExecutionItemStatus::Rejected);
    QVERIFY(!result.resumedPublishedResult);
    QCOMPARE(readTestFile(source), QByteArrayLiteral("source"));
    QCOMPARE(readTestFile(destination), QByteArrayLiteral("destination"));
}

void FileOperatorTest::resumePublishedCleanupSetsResumedFlagOnSuccess()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source =
        QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString destination =
        QDir(directory.path()).filePath(QStringLiteral("destination.txt"));
    const QByteArray contents = QByteArrayLiteral("published");
    QVERIFY(writeTestFile(source, contents));
    QVERIFY(writeTestFile(destination, contents));

    FilesystemPathInfo sourceInfo;
    FilesystemPathInfo destinationInfo;
    QString error;
    QVERIFY(inspectFilesystemPath(source, sourceInfo, error));
    QVERIFY(inspectFilesystemPath(destination, destinationInfo, error));
    QVERIFY(sourceInfo.identity.valid);
    QVERIFY(destinationInfo.identity.valid);

    PublishedMoveRecoveryState recoveryState;
    recoveryState.executionId = QStringLiteral("execution-success");
    recoveryState.planGeneration = 1;
    recoveryState.scanGeneration = 1;
    recoveryState.policy = ConflictPolicy::AutoRename;
    recoveryState.sourcePath = source;
    recoveryState.destinationPath = destination;
    recoveryState.sourceIdentity = sourceInfo.identity;
    recoveryState.destinationIdentity = destinationInfo.identity;
    recoveryState.sourceSize = sourceInfo.size;
    recoveryState.destinationSize = destinationInfo.size;
    recoveryState.sourceDigest =
        QCryptographicHash::hash(contents, QCryptographicHash::Sha256);
    recoveryState.destinationDigest = recoveryState.sourceDigest;
    QVERIFY(recoveryState.isValid());

    const FileMoveResult result = FileOperator().resumePublishedCleanup(
        FileMoveRequest{
            source,
            destination,
            ConflictDecisionAction::Proceed,
        },
        recoveryState,
        std::atomic_bool{false});

    QCOMPARE(result.status, ExecutionItemStatus::Succeeded);
    QVERIFY(result.resumedPublishedResult);
    QVERIFY(!QFileInfo::exists(source));
    QCOMPARE(readTestFile(destination), contents);
}

} // namespace Test
} // namespace FilePilot
