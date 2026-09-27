#include "BackupPhase1Tests.h"

#include "core/backup/BackupInventoryBuilder.h"
#include "core/backup/BackupPlanTypes.h"
#include "core/backup/BackupPrevalidator.h"
#include "core/filesystem/FileHasher.h"
#include "core/filesystem/FileIdentity.h"
#include "core/filesystem/FileSnapshot.h"
#include "core/model/AppError.h"
#include "core/model/ConflictPolicy.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QStorageInfo>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <memory>
#include <type_traits>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace FilePilot {
namespace BackupTest {

namespace {

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
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QByteArray sha256(const QByteArray &contents)
{
    return QCryptographicHash::hash(contents, QCryptographicHash::Sha256);
}

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

QStringList inventoryReference(const QString &root)
{
    QStringList entries;
    QDirIterator iterator(root,
                          QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        const QString relative =
            QDir(root).relativeFilePath(iterator.filePath());
        entries.append(iterator.fileInfo().isDir()
                           ? relative + QLatin1Char('/')
                           : relative);
    }
    entries.sort();
    return entries;
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

bool createSymbolicLink(const QString &linkPath, const QString &targetPath)
{
    QDir().mkpath(QFileInfo(linkPath).absolutePath());
    return CreateSymbolicLinkW(
               reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(linkPath).utf16()),
               reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(targetPath).utf16()),
               SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)
        != FALSE;
}

class ListDirectoryDenyGuard
{
public:
    explicit ListDirectoryDenyGuard(QString path)
        : path_(std::move(path))
    {
        denied_ = applyDeny();
    }

    ~ListDirectoryDenyGuard()
    {
        applyReset();
    }

    ListDirectoryDenyGuard(const ListDirectoryDenyGuard &) = delete;
    ListDirectoryDenyGuard &operator=(const ListDirectoryDenyGuard &) = delete;

    bool isDenied() const
    {
        return denied_;
    }

private:
    bool runIcacls(const QStringList &arguments) const
    {
        QProcess process;
        process.start(QStringLiteral("icacls.exe"), arguments);
        return process.waitForFinished(10000)
            && process.exitStatus() == QProcess::NormalExit
            && process.exitCode() == 0;
    }

    bool applyDeny() const
    {
        const QString user = QStringLiteral("%1\\%2")
            .arg(QString::fromLocal8Bit(qgetenv("USERDOMAIN")),
                 QString::fromLocal8Bit(qgetenv("USERNAME")));
        return runIcacls({
            QDir::toNativeSeparators(path_),
            QStringLiteral("/deny"),
            user + QStringLiteral(":(OI)(CI)RD"),
        });
    }

    void applyReset() const
    {
        runIcacls({
            QDir::toNativeSeparators(path_),
            QStringLiteral("/reset"),
            QStringLiteral("/T"),
            QStringLiteral("/C"),
        });
    }

    QString path_;
    bool denied_ = false;
};
#endif

QStringList inventoryPaths(const BackupInventory &inventory)
{
    QStringList entries;
    for (const BackupPlanItem &item : inventory.items) {
        entries.append(item.kind == BackupEntryKind::Directory
                           ? item.relativePath + QLatin1Char('/')
                           : item.relativePath);
    }
    entries.sort();
    return entries;
}

bool inventoryContains(const BackupInventory &inventory, const QString &path)
{
    return inventoryPaths(inventory).contains(path);
}

} // namespace

void FileSnapshotContractTest::capturesRegularFileValues()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = QDir(directory.path()).filePath(QStringLiteral("file.txt"));
    const QByteArray contents = QByteArrayLiteral("snapshot");
    QVERIFY(writeTestFile(path, contents));

    FileIdentity identity;
    QVERIFY(inspectIdentity(path, identity));
    QCOMPARE(QFileInfo(path).size(), qint64(contents.size()));
    QVERIFY(QFileInfo(path).lastModified().isValid());
    QCOMPARE(sha256(contents), QCryptographicHash::hash(contents, QCryptographicHash::Sha256));

    FileSnapshot snapshot;
    AppError error;
    QVERIFY(captureFileSnapshot(path, snapshot, error));
    QVERIFY(!error.isValid());
    QVERIFY(snapshot.valid);
    QCOMPARE(snapshot.path, path);
    QCOMPARE(snapshot.kind, FilesystemPathKind::RegularFile);
    QCOMPARE(snapshot.identity, identity);
    QCOMPARE(snapshot.size, qint64(contents.size()));
    QCOMPARE(snapshot.modifiedTime.toMSecsSinceEpoch(),
             QFileInfo(path).lastModified().toMSecsSinceEpoch());
    QCOMPARE(snapshot.sha256, sha256(contents));
    QVERIFY(!snapshot.reparsePoint);
}

void FileSnapshotContractTest::rejectsMissingFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString missing = QDir(directory.path()).filePath(QStringLiteral("missing.txt"));
    QVERIFY(!QFileInfo::exists(missing));

    FileSnapshot snapshot;
    AppError error;
    QVERIFY(!captureFileSnapshot(missing, snapshot, error));
    QVERIFY(!snapshot.valid);
    QVERIFY(error.isValid());
    QCOMPARE(error.code(), ErrorCode::NotFound);
}

void FileSnapshotContractTest::detectsReplacementIdentityChange()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = QDir(directory.path()).filePath(QStringLiteral("file.txt"));
    QVERIFY(writeTestFile(path, QByteArrayLiteral("first")));
    FileIdentity first;
    QVERIFY(inspectIdentity(path, first));

    QVERIFY(QFile::remove(path));
    QVERIFY(writeTestFile(path, QByteArrayLiteral("second")));
    FileIdentity second;
    QVERIFY(inspectIdentity(path, second));
    QVERIFY(first != second);

    FileSnapshot firstSnapshot;
    FileSnapshot secondSnapshot;
    AppError firstError;
    AppError secondError;
    QVERIFY(QFile::remove(path));
    QVERIFY(writeTestFile(path, QByteArrayLiteral("first")));
    QVERIFY(captureFileSnapshot(path, firstSnapshot, firstError));
    QVERIFY(QFile::remove(path));
    QVERIFY(writeTestFile(path, QByteArrayLiteral("second")));
    QVERIFY(captureFileSnapshot(path, secondSnapshot, secondError));
    QVERIFY(firstSnapshot.identity != secondSnapshot.identity);
}

void FileSnapshotContractTest::detectsSameSizeContentChange()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = QDir(directory.path()).filePath(QStringLiteral("file.txt"));
    QVERIFY(writeTestFile(path, QByteArrayLiteral("aaaa")));
    FileIdentity firstIdentity;
    QVERIFY(inspectIdentity(path, firstIdentity));
    const QByteArray firstDigest = sha256(QByteArrayLiteral("aaaa"));

    QVERIFY(writeTestFile(path, QByteArrayLiteral("bbbb")));
    FileIdentity secondIdentity;
    QVERIFY(inspectIdentity(path, secondIdentity));
    QCOMPARE(QFileInfo(path).size(), qint64(4));
    QCOMPARE(firstIdentity, secondIdentity);
    QVERIFY(firstDigest != sha256(QByteArrayLiteral("bbbb")));

    FileSnapshot firstSnapshot;
    FileSnapshot secondSnapshot;
    AppError firstError;
    AppError secondError;
    QVERIFY(writeTestFile(path, QByteArrayLiteral("aaaa")));
    QVERIFY(captureFileSnapshot(path, firstSnapshot, firstError));
    QVERIFY(writeTestFile(path, QByteArrayLiteral("bbbb")));
    QVERIFY(captureFileSnapshot(path, secondSnapshot, secondError));
    QCOMPARE(firstSnapshot.size, secondSnapshot.size);
    QCOMPARE(firstSnapshot.identity, secondSnapshot.identity);
    QVERIFY(firstSnapshot.sha256 != secondSnapshot.sha256);
}

void FileSnapshotContractTest::detectsSizeChange()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = QDir(directory.path()).filePath(QStringLiteral("file.txt"));
    QVERIFY(writeTestFile(path, QByteArrayLiteral("aa")));
    QVERIFY(writeTestFile(path, QByteArrayLiteral("aaa")));
    QVERIFY(QFileInfo(path).size() != 2);

    FileSnapshot firstSnapshot;
    FileSnapshot secondSnapshot;
    AppError firstError;
    AppError secondError;
    QVERIFY(writeTestFile(path, QByteArrayLiteral("aa")));
    QVERIFY(captureFileSnapshot(path, firstSnapshot, firstError));
    QVERIFY(writeTestFile(path, QByteArrayLiteral("aaa")));
    QVERIFY(captureFileSnapshot(path, secondSnapshot, secondError));
    QCOMPARE(firstSnapshot.size, qint64(2));
    QCOMPARE(secondSnapshot.size, qint64(3));
    QVERIFY(firstSnapshot.size != secondSnapshot.size);
}

void FileSnapshotContractTest::detectsDigestMismatch()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString first = QDir(directory.path()).filePath(QStringLiteral("first.txt"));
    const QString second = QDir(directory.path()).filePath(QStringLiteral("second.txt"));
    QVERIFY(writeTestFile(first, QByteArrayLiteral("same")));
    QVERIFY(writeTestFile(second, QByteArrayLiteral("different")));
    QVERIFY(sha256(readTestFile(first)) != sha256(readTestFile(second)));

    FileSnapshot firstSnapshot;
    FileSnapshot secondSnapshot;
    AppError firstError;
    AppError secondError;
    QVERIFY(captureFileSnapshot(first, firstSnapshot, firstError));
    QVERIFY(captureFileSnapshot(second, secondSnapshot, secondError));
    QVERIFY(firstSnapshot.sha256 != secondSnapshot.sha256);
}

void FileSnapshotContractTest::capturesDirectoryBasicInformation()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = QDir(directory.path()).filePath(QStringLiteral("directory"));
    QVERIFY(QDir().mkpath(path));

    FilesystemPathInfo info;
    QString error;
    QVERIFY(inspectFilesystemPath(path, info, error));
    QVERIFY(info.isDirectory());
    QVERIFY(info.identity.valid);

    FileSnapshot snapshot;
    AppError snapshotError;
    QVERIFY(captureFileSnapshot(path, snapshot, snapshotError));
    QVERIFY(snapshot.valid);
    QCOMPARE(snapshot.path, path);
    QCOMPARE(snapshot.kind, FilesystemPathKind::Directory);
    QCOMPARE(snapshot.identity, info.identity);
    QVERIFY(snapshot.modifiedTime.isValid());
    QVERIFY(!snapshot.reparsePoint);
}

void FileSnapshotContractTest::capturesReparseInformation()
{
#ifdef Q_OS_WIN
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = QDir(directory.path()).filePath(QStringLiteral("root"));
    const QString target = QDir(directory.path()).filePath(QStringLiteral("target"));
    const QString junction = QDir(root).filePath(QStringLiteral("junction"));
    QVERIFY(QDir().mkpath(root));
    QVERIFY(QDir().mkpath(target));
    QVERIFY(createJunction(junction, target));

    FilesystemPathInfo info;
    QString error;
    QVERIFY(inspectFilesystemPath(junction, info, error));
    QVERIFY(info.isReparsePoint());
    QVERIFY(info.identity.valid);

    FileSnapshot snapshot;
    AppError snapshotError;
    QVERIFY(captureFileSnapshot(junction, snapshot, snapshotError));
    QVERIFY(snapshot.valid);
    QCOMPARE(snapshot.kind, FilesystemPathKind::ReparsePoint);
    QVERIFY(snapshot.reparsePoint);
    QCOMPARE(snapshot.identity, info.identity);
#else
    QSKIP("Windows reparse-point fixture");
#endif
}

void FileHasherContractTest::hashesFixedSmallFile()
{
    QCOMPARE(sha256(QByteArrayLiteral("abc")),
             QByteArray::fromHex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = QDir(directory.path()).filePath(QStringLiteral("small.txt"));
    QVERIFY(writeTestFile(path, QByteArrayLiteral("abc")));
    QByteArray digest;
    AppError error;
    QVERIFY(FileHasher().hashFile(path, digest, error));
    QVERIFY(!error.isValid());
    QCOMPARE(digest, QByteArray::fromHex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}

void FileHasherContractTest::hashesEmptyFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = QDir(directory.path()).filePath(QStringLiteral("empty.txt"));
    QVERIFY(writeTestFile(path, {}));
    QCOMPARE(QFileInfo(path).size(), qint64(0));
    QCOMPARE(sha256({}),
             QByteArray::fromHex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));

    QByteArray digest;
    AppError error;
    QVERIFY(FileHasher().hashFile(path, digest, error));
    QVERIFY(!error.isValid());
    QCOMPARE(digest, QByteArray::fromHex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
}

void FileHasherContractTest::hashesMultiKilobyteFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = QDir(directory.path()).filePath(QStringLiteral("multi.bin"));
    const QByteArray contents(8192, 'A');
    QVERIFY(writeTestFile(path, contents));
    QCOMPARE(QFileInfo(path).size(), qint64(contents.size()));
    QVERIFY(QFileInfo(path).lastModified().isValid());
    QCOMPARE(sha256(contents), QCryptographicHash::hash(contents, QCryptographicHash::Sha256));

    QByteArray digest;
    AppError error;
    QVERIFY(FileHasher().hashFile(path, digest, error));
    QVERIFY(!error.isValid());
    QCOMPARE(digest, sha256(contents));
}

void FileHasherContractTest::hashesBinaryFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = QDir(directory.path()).filePath(QStringLiteral("binary.bin"));
    const QByteArray contents = QByteArray::fromHex("000102fffe");
    QVERIFY(writeTestFile(path, contents));
    QCOMPARE(sha256(contents), QCryptographicHash::hash(contents, QCryptographicHash::Sha256));

    QByteArray digest;
    AppError error;
    QVERIFY(FileHasher().hashFile(path, digest, error));
    QVERIFY(!error.isValid());
    QCOMPARE(digest, sha256(contents));
}

void FileHasherContractTest::sameContentHasSameHash()
{
    QCOMPARE(sha256(QByteArrayLiteral("same")), sha256(QByteArrayLiteral("same")));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString first = QDir(directory.path()).filePath(QStringLiteral("first.txt"));
    const QString second = QDir(directory.path()).filePath(QStringLiteral("second.txt"));
    QVERIFY(writeTestFile(first, QByteArrayLiteral("same")));
    QVERIFY(writeTestFile(second, QByteArrayLiteral("same")));

    QByteArray firstDigest;
    QByteArray secondDigest;
    AppError firstError;
    AppError secondError;
    QVERIFY(FileHasher().hashFile(first, firstDigest, firstError));
    QVERIFY(FileHasher().hashFile(second, secondDigest, secondError));
    QCOMPARE(firstDigest, secondDigest);
}

void FileHasherContractTest::differentContentHasDifferentHash()
{
    QVERIFY(sha256(QByteArrayLiteral("left")) != sha256(QByteArrayLiteral("right")));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString first = QDir(directory.path()).filePath(QStringLiteral("first.txt"));
    const QString second = QDir(directory.path()).filePath(QStringLiteral("second.txt"));
    QVERIFY(writeTestFile(first, QByteArrayLiteral("left")));
    QVERIFY(writeTestFile(second, QByteArrayLiteral("right")));

    QByteArray firstDigest;
    QByteArray secondDigest;
    AppError firstError;
    AppError secondError;
    QVERIFY(FileHasher().hashFile(first, firstDigest, firstError));
    QVERIFY(FileHasher().hashFile(second, secondDigest, secondError));
    QVERIFY(firstDigest != secondDigest);
}

void FileHasherContractTest::reportsReadFailure()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString missing = QDir(directory.path()).filePath(QStringLiteral("missing.bin"));
    QVERIFY(!QFileInfo::exists(missing));

    QByteArray digest;
    AppError error;
    QVERIFY(!FileHasher().hashFile(missing, digest, error));
    QVERIFY(digest.isEmpty());
    QVERIFY(error.isValid());
    QCOMPARE(error.code(), ErrorCode::NotFound);
}

void BackupPlanContractTest::fileSourceCarriesSafetyMetadata()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("payload")));
    FileIdentity identity;
    QVERIFY(inspectIdentity(source, identity));
    QCOMPARE(QFileInfo(source).size(), qint64(7));

    const QString destinationRoot = QDir(directory.path()).filePath(QStringLiteral("backup"));
    QVERIFY(QDir().mkpath(destinationRoot));
    const QString destination = QDir(destinationRoot).filePath(QStringLiteral("source.txt"));

    FileSnapshot sourceSnapshot;
    AppError snapshotError;
    QVERIFY(captureFileSnapshot(source, sourceSnapshot, snapshotError));

    BackupPlanItem item;
    item.relativePath = QFileInfo(source).fileName();
    item.sourcePath = source;
    item.plannedDestinationPath = destination;
    item.kind = BackupEntryKind::File;
    item.expectedSourceIdentity = identity;
    item.expectedSize = sourceSnapshot.size;
    item.expectedModifiedTime = sourceSnapshot.modifiedTime;
    item.sourceSnapshot = sourceSnapshot;
    item.conflictPolicy = ConflictPolicy::AutoRename;
    item.plannedStatus = BackupPlanItemStatus::Ready;

    BackupPlan plan;
    plan.operationId = QStringLiteral("operation-1");
    plan.sourcePath = source;
    plan.sourceKind = BackupEntryKind::File;
    plan.destinationRoot = destinationRoot;
    plan.finalDestinationPath = destination;
    plan.conflictPolicy = ConflictPolicy::AutoRename;
    plan.sourceRootSnapshot = sourceSnapshot;
    plan.items.push_back(item);
    plan.expectedFileCount = 1;
    plan.expectedDirectoryCount = 0;
    plan.expectedBytes = sourceSnapshot.size;
    plan.planGeneration = 1;

    QCOMPARE(plan.sourceKind, BackupEntryKind::File);
    QCOMPARE(plan.items.size(), std::size_t(1));
    QCOMPARE(plan.items.front().expectedSize, qint64(7));
    QCOMPARE(plan.items.front().sourceSnapshot.sha256, sha256(QByteArrayLiteral("payload")));
    QCOMPARE(plan.expectedBytes, qint64(7));
}

void BackupPlanContractTest::directorySourceUsesRelativePath()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString sourceRoot = QDir(directory.path()).filePath(QStringLiteral("root"));
    const QString nested = QDir(sourceRoot).filePath(QStringLiteral("nested"));
    QVERIFY(QDir().mkpath(nested));
    QCOMPARE(QDir(sourceRoot).relativeFilePath(nested), QStringLiteral("nested"));

    BackupPlanItem item;
    item.relativePath = QDir(sourceRoot).relativeFilePath(nested);
    item.sourcePath = nested;
    item.plannedDestinationPath =
        QDir(QDir(directory.path()).filePath(QStringLiteral("backup/root")))
            .filePath(QStringLiteral("nested"));
    item.kind = BackupEntryKind::Directory;
    item.plannedStatus = BackupPlanItemStatus::Ready;

    BackupPlan plan;
    plan.sourcePath = sourceRoot;
    plan.sourceKind = BackupEntryKind::Directory;
    plan.items.push_back(item);

    QCOMPARE(plan.sourceKind, BackupEntryKind::Directory);
    QCOMPARE(plan.items.front().relativePath, QStringLiteral("nested"));
    QVERIFY(!plan.items.front().relativePath.startsWith(QLatin1Char('/')));
}

void BackupPlanContractTest::planItemExposesConflictAndDestinationIdentity()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("source")));
    const QString destination = QDir(directory.path()).filePath(QStringLiteral("backup/source.txt"));
    QVERIFY(writeTestFile(destination, QByteArrayLiteral("old")));
    FileIdentity sourceIdentity;
    FileIdentity destinationIdentity;
    QVERIFY(inspectIdentity(source, sourceIdentity));
    QVERIFY(inspectIdentity(destination, destinationIdentity));

    FileSnapshot sourceSnapshot;
    AppError snapshotError;
    QVERIFY(captureFileSnapshot(source, sourceSnapshot, snapshotError));

    BackupPlanItem item;
    item.relativePath = QStringLiteral("source.txt");
    item.sourcePath = source;
    item.plannedDestinationPath = destination;
    item.kind = BackupEntryKind::File;
    item.expectedSourceIdentity = sourceIdentity;
    item.expectedSize = sourceSnapshot.size;
    item.expectedModifiedTime = sourceSnapshot.modifiedTime;
    item.sourceSnapshot = sourceSnapshot;
    item.conflictPolicy = ConflictPolicy::Overwrite;
    item.expectedDestinationIdentity = destinationIdentity;
    item.plannedStatus = BackupPlanItemStatus::Ready;

    QCOMPARE(item.expectedSourceIdentity, sourceIdentity);
    QCOMPARE(item.expectedDestinationIdentity, destinationIdentity);
    QCOMPARE(item.sourceSnapshot.sha256, sha256(QByteArrayLiteral("source")));
    QCOMPARE(item.conflictPolicy, ConflictPolicy::Overwrite);
    QCOMPARE(item.plannedStatus, BackupPlanItemStatus::Ready);
}

void BackupPlanContractTest::planIsDataModelWithoutUiState()
{
    static_assert(std::is_default_constructible_v<BackupPlan>);
    static_assert(std::is_default_constructible_v<BackupPlanItem>);
    static_assert(std::is_default_constructible_v<BackupUnsupportedEntry>);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(QDir().mkpath(directory.path()));

    BackupPlan plan;
    BackupPlanItem item;
    item.plannedStatus = BackupPlanItemStatus::Ready;
    plan.items.push_back(item);
    QCOMPARE(plan.items.front().plannedStatus, BackupPlanItemStatus::Ready);
    QVERIFY(plan.operationId.isEmpty());
}

void BackupInventoryBuilderContractTest::buildsNestedInventoryIncludingEmptyDirectories()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = QDir(directory.path()).filePath(QStringLiteral("root"));
    QVERIFY(QDir().mkpath(QDir(root).filePath(QStringLiteral("nested"))));
    QVERIFY(QDir().mkpath(QDir(root).filePath(QStringLiteral("empty"))));
    QVERIFY(writeTestFile(QDir(root).filePath(QStringLiteral("a.txt")), QByteArrayLiteral("a")));
    QVERIFY(writeTestFile(QDir(root).filePath(QStringLiteral("nested/b.txt")), QByteArrayLiteral("b")));

    const QStringList expected{
        QStringLiteral("a.txt"),
        QStringLiteral("empty/"),
        QStringLiteral("nested/"),
        QStringLiteral("nested/b.txt"),
    };
    QCOMPARE(inventoryReference(root), expected);

    BackupInventory inventory;
    AppError error;
    QVERIFY(BackupInventoryBuilder().build(root, inventory, error));
    QVERIFY(!error.isValid());
    QCOMPARE(inventoryPaths(inventory), expected);
    QCOMPARE(inventory.expectedFileCount, qint64(2));
    QCOMPARE(inventory.expectedDirectoryCount, qint64(2));
    QCOMPARE(inventory.expectedBytes, qint64(2));
}

void BackupInventoryBuilderContractTest::preservesDirectoryStructure()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = QDir(directory.path()).filePath(QStringLiteral("root"));
    QVERIFY(QDir().mkpath(QDir(root).filePath(QStringLiteral("nested/deep"))));
    QVERIFY(writeTestFile(QDir(root).filePath(QStringLiteral("nested/deep/file.txt")), QByteArrayLiteral("x")));
    QVERIFY(inventoryReference(root).contains(QStringLiteral("nested/deep/")));
    QVERIFY(inventoryReference(root).contains(QStringLiteral("nested/deep/file.txt")));

    BackupInventory inventory;
    AppError error;
    QVERIFY(BackupInventoryBuilder().build(root, inventory, error));
    QVERIFY(!error.isValid());
    QVERIFY(inventoryContains(inventory, QStringLiteral("nested/")));
    QVERIFY(inventoryContains(inventory, QStringLiteral("nested/deep/")));
    QVERIFY(inventoryContains(inventory, QStringLiteral("nested/deep/file.txt")));
}

void BackupInventoryBuilderContractTest::reportsJunctionAsUnsupported()
{
#ifdef Q_OS_WIN
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = QDir(directory.path()).filePath(QStringLiteral("root"));
    const QString target = QDir(directory.path()).filePath(QStringLiteral("target"));
    const QString junction = QDir(root).filePath(QStringLiteral("junction"));
    QVERIFY(QDir().mkpath(root));
    QVERIFY(QDir().mkpath(target));
    QVERIFY(createJunction(junction, target));

    FilesystemPathInfo info;
    QString error;
    QVERIFY(inspectFilesystemPath(junction, info, error));
    QVERIFY(info.isReparsePoint());

    BackupInventory inventory;
    AppError inventoryError;
    QVERIFY(BackupInventoryBuilder().build(root, inventory, inventoryError));
    QVERIFY(!inventoryError.isValid());
    QCOMPARE(inventory.unsupportedEntries.size(), std::size_t(1));
    QCOMPARE(inventory.unsupportedEntries.front().relativePath, QStringLiteral("junction"));
    QCOMPARE(inventory.unsupportedEntries.front().kind, FilesystemPathKind::ReparsePoint);
    QVERIFY(!inventory.unsupportedEntries.front().reason.isEmpty());
#else
    QSKIP("Windows Junction fixture");
#endif
}

void BackupInventoryBuilderContractTest::reportsSymbolicLinkAsUnsupported()
{
#ifdef Q_OS_WIN
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = QDir(directory.path()).filePath(QStringLiteral("root"));
    const QString target = QDir(directory.path()).filePath(QStringLiteral("target"));
    const QString link = QDir(root).filePath(QStringLiteral("link"));
    QVERIFY(QDir().mkpath(root));
    QVERIFY(QDir().mkpath(target));
    if (!createSymbolicLink(link, target)) {
        QSKIP("Unable to create symbolic link fixture");
    }

    FilesystemPathInfo info;
    QString error;
    QVERIFY(inspectFilesystemPath(link, info, error));
    QVERIFY(info.isReparsePoint());

    BackupInventory inventory;
    AppError inventoryError;
    QVERIFY(BackupInventoryBuilder().build(root, inventory, inventoryError));
    QVERIFY(!inventoryError.isValid());
    QCOMPARE(inventory.unsupportedEntries.size(), std::size_t(1));
    QCOMPARE(inventory.unsupportedEntries.front().relativePath, QStringLiteral("link"));
    QCOMPARE(inventory.unsupportedEntries.front().kind, FilesystemPathKind::ReparsePoint);
    QVERIFY(!inventory.unsupportedEntries.front().reason.isEmpty());
#else
    QSKIP("Windows symbolic-link fixture");
#endif
}

void BackupPrevalidatorContractTest::rejectsMissingSourceRoot()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("missing-source"));
    const QString destination = QDir(directory.path()).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destination));
    QVERIFY(!QFileInfo::exists(source));

    const BackupValidationResult result =
        BackupPrevalidator().validateRoots(source, destination);
    QVERIFY(!result.valid);
    QCOMPARE(result.code, BackupValidationCode::MissingSource);
    QVERIFY(result.error.isValid());
}

void BackupPrevalidatorContractTest::acceptsMissingDestinationRootWhenCreatable()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source"));
    const QString destination = QDir(directory.path()).filePath(QStringLiteral("new-destination"));
    QVERIFY(QDir().mkpath(source));
    QVERIFY(!QFileInfo::exists(destination));
    QVERIFY(QFileInfo(QFileInfo(destination).absolutePath()).isDir());

    const BackupValidationResult result =
        BackupPrevalidator().validateRoots(source, destination);
    QVERIFY(result.valid);
    QCOMPARE(result.code, BackupValidationCode::Valid);
    QVERIFY(!result.error.isValid());
    QVERIFY(result.sourceVolume.valid);
    QVERIFY(result.destinationVolume.valid);
    QCOMPARE(result.destinationRootSnapshot.kind, FilesystemPathKind::Missing);
    QVERIFY(!result.destinationRootSnapshot.valid);
}

void BackupPrevalidatorContractTest::rejectsEqualAndCaseEquivalentRoots()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("root"));
    QVERIFY(QDir().mkpath(source));
    QCOMPARE(source.compare(source.toUpper(), Qt::CaseInsensitive), 0);

    const BackupValidationResult result =
        BackupPrevalidator().validateRoots(source, source.toUpper());
    QVERIFY(!result.valid);
    QCOMPARE(result.code, BackupValidationCode::PathRelationshipInvalid);
    QVERIFY(result.error.isValid());
}

void BackupPrevalidatorContractTest::rejectsNestedRootsInBothDirections()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source"));
    const QString destination = QDir(source).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destination));
    QVERIFY(QDir(destination).absolutePath().startsWith(QDir(source).absolutePath()));

    const BackupValidationResult sourceContainsDestination =
        BackupPrevalidator().validateRoots(source, destination);
    QVERIFY(!sourceContainsDestination.valid);
    QCOMPARE(sourceContainsDestination.code, BackupValidationCode::PathRelationshipInvalid);

    const BackupValidationResult destinationContainsSource =
        BackupPrevalidator().validateRoots(destination, source);
    QVERIFY(!destinationContainsSource.valid);
    QCOMPARE(destinationContainsSource.code, BackupValidationCode::PathRelationshipInvalid);
}

void BackupPrevalidatorContractTest::rejectsReparsePointRoots()
{
#ifdef Q_OS_WIN
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source"));
    const QString target = QDir(directory.path()).filePath(QStringLiteral("target"));
    const QString junction = QDir(directory.path()).filePath(QStringLiteral("source-link"));
    QVERIFY(QDir().mkpath(source));
    QVERIFY(QDir().mkpath(target));
    QVERIFY(createJunction(junction, target));
    FilesystemPathInfo info;
    QString error;
    QVERIFY(inspectFilesystemPath(junction, info, error));
    QVERIFY(info.isReparsePoint());

    const BackupValidationResult reparseDestination =
        BackupPrevalidator().validateRoots(source, junction);
    QVERIFY(!reparseDestination.valid);
    QCOMPARE(reparseDestination.code, BackupValidationCode::UnsupportedReparse);

    const BackupValidationResult reparseSource =
        BackupPrevalidator().validateRoots(junction, target);
    QVERIFY(!reparseSource.valid);
    QCOMPARE(reparseSource.code, BackupValidationCode::UnsupportedReparse);
#else
    QSKIP("Windows reparse-point fixture");
#endif
}

void BackupPrevalidatorContractTest::exposesVolumeInformation()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
#ifdef Q_OS_WIN
    const QString rootPath = QDir::toNativeSeparators(QDir(directory.path()).absolutePath());
    const QString volumeRoot = rootPath.left(3);
    DWORD serial = 0;
    QVERIFY(GetVolumeInformationW(
                reinterpret_cast<LPCWSTR>(volumeRoot.utf16()),
                nullptr,
                0,
                &serial,
                nullptr,
                nullptr,
                nullptr,
                0)
            != FALSE);
    QVERIFY(serial != 0);
#endif
    QVERIFY(QStorageInfo(directory.path()).isValid());

    const QString source = QDir(directory.path()).filePath(QStringLiteral("source"));
    const QString destination = QDir(directory.path()).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(source));
    QVERIFY(QDir().mkpath(destination));

    const BackupValidationResult result =
        BackupPrevalidator().validateRoots(source, destination);
    QVERIFY(result.valid);
    QVERIFY(result.sourceVolume.valid);
    QVERIFY(result.destinationVolume.valid);
    QVERIFY(!result.sourceVolume.rootPath.isEmpty());
    QVERIFY(!result.destinationVolume.rootPath.isEmpty());
#ifdef Q_OS_WIN
    QCOMPARE(result.sourceVolume.serial, quint32(serial));
    QCOMPARE(result.destinationVolume.serial, quint32(serial));
#endif
}

void BackupPrevalidatorContractTest::rejectsInvalidAndOverlongPaths()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString invalid = QDir(directory.path()).filePath(QStringLiteral("bad:name"));
    const QString overlong = QDir(directory.path()).filePath(QString(5000, QLatin1Char('a')));
    QVERIFY(!invalid.isEmpty());
    QVERIFY(overlong.size() > 4096);

    const QString destination = QDir(directory.path()).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destination));

    const BackupValidationResult invalidResult =
        BackupPrevalidator().validateRoots(invalid, destination);
    QVERIFY(!invalidResult.valid);
    QCOMPARE(invalidResult.code, BackupValidationCode::InvalidPath);
    QVERIFY(invalidResult.error.isValid());

    const BackupValidationResult overlongResult =
        BackupPrevalidator().validateRoots(overlong, destination);
    QVERIFY(!overlongResult.valid);
    QCOMPARE(overlongResult.code, BackupValidationCode::InvalidPath);
    QVERIFY(overlongResult.error.isValid());
}


void BackupInventoryBuilderContractTest::reportsPermissionDeniedDirectory()
{
#ifdef Q_OS_WIN
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = QDir(directory.path()).filePath(QStringLiteral("root"));
    QVERIFY(QDir().mkpath(root));
    QVERIFY(writeTestFile(QDir(root).filePath(QStringLiteral("hidden.txt")),
                         QByteArrayLiteral("hidden")));

    FileSnapshot rootSnapshot;
    AppError snapshotError;
    QVERIFY(captureFileSnapshot(root, rootSnapshot, snapshotError));

    ListDirectoryDenyGuard deny(root);
    QVERIFY(deny.isDenied());

    BackupInventory inventory;
    AppError error;
    QVERIFY(!BackupInventoryBuilder().build(root, inventory, error));
    QVERIFY(error.isValid());
    QCOMPARE(error.code(), ErrorCode::AccessDenied);
    QVERIFY(inventory.items.empty());
    QVERIFY(inventory.unsupportedEntries.empty());
#else
    QSKIP("Windows ACL fixture");
#endif
}

void BackupInventoryBuilderContractTest::reportsChildEnumerationFailure()
{
#ifdef Q_OS_WIN
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = QDir(directory.path()).filePath(QStringLiteral("root"));
    const QString locked = QDir(root).filePath(QStringLiteral("locked"));
    QVERIFY(QDir().mkpath(locked));
    QVERIFY(writeTestFile(QDir(locked).filePath(QStringLiteral("hidden.txt")),
                         QByteArrayLiteral("hidden")));

    FileSnapshot childSnapshot;
    AppError snapshotError;
    QVERIFY(captureFileSnapshot(locked, childSnapshot, snapshotError));

    ListDirectoryDenyGuard deny(locked);
    QVERIFY(deny.isDenied());

    BackupInventory inventory;
    AppError error;
    QVERIFY(!BackupInventoryBuilder().build(root, inventory, error));
    QVERIFY(error.isValid());
    QCOMPARE(error.code(), ErrorCode::AccessDenied);
    QVERIFY(inventory.items.empty());
    QVERIFY(inventory.unsupportedEntries.empty());
#else
    QSKIP("Windows ACL fixture");
#endif
}

void BackupInventoryBuilderContractTest::emptyDirectoryRemainsComplete()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = QDir(directory.path()).filePath(QStringLiteral("root"));
    QVERIFY(QDir().mkpath(QDir(root).filePath(QStringLiteral("empty"))));

    BackupInventory inventory;
    AppError error;
    QVERIFY(BackupInventoryBuilder().build(root, inventory, error));
    QVERIFY(!error.isValid());
    QCOMPARE(inventory.expectedFileCount, qint64(0));
    QCOMPARE(inventory.expectedDirectoryCount, qint64(1));
    QVERIFY(inventoryContains(inventory, QStringLiteral("empty/")));
}

void BackupInventoryBuilderContractTest::normalDirectoryRemainsComplete()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = QDir(directory.path()).filePath(QStringLiteral("root"));
    QVERIFY(QDir().mkpath(QDir(root).filePath(QStringLiteral("nested"))));
    QVERIFY(writeTestFile(QDir(root).filePath(QStringLiteral("nested/file.txt")),
                         QByteArrayLiteral("x")));

    BackupInventory inventory;
    AppError error;
    QVERIFY(BackupInventoryBuilder().build(root, inventory, error));
    QVERIFY(!error.isValid());
    QCOMPARE(inventory.expectedFileCount, qint64(1));
    QCOMPARE(inventory.expectedDirectoryCount, qint64(1));
    QVERIFY(inventoryContains(inventory, QStringLiteral("nested/")));
    QVERIFY(inventoryContains(inventory, QStringLiteral("nested/file.txt")));
}

void BackupInventoryBuilderContractTest::enumerationFailureIsNotComplete()
{
#ifdef Q_OS_WIN
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = QDir(directory.path()).filePath(QStringLiteral("root"));
    QVERIFY(QDir().mkpath(root));
    QVERIFY(writeTestFile(QDir(root).filePath(QStringLiteral("hidden.txt")),
                         QByteArrayLiteral("hidden")));

    ListDirectoryDenyGuard deny(root);
    QVERIFY(deny.isDenied());

    BackupInventory inventory;
    AppError error;
    QVERIFY(!BackupInventoryBuilder().build(root, inventory, error));
    QVERIFY(error.isValid());
    QVERIFY(inventory.items.empty());
    QVERIFY(inventory.expectedFileCount == 0);
    QVERIFY(inventory.expectedDirectoryCount == 0);
    QVERIFY(inventory.expectedBytes == 0);
#else
    QSKIP("Windows ACL fixture");
#endif
}

void BackupPrevalidatorContractTest::rejectsIntermediateJunctionEqualTarget()
{
#ifdef Q_OS_WIN
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString actual = QDir(directory.path()).filePath(QStringLiteral("actual"));
    const QString source = QDir(actual).filePath(QStringLiteral("source"));
    const QString aliasRoot = QDir(directory.path()).filePath(QStringLiteral("aliasroot"));
    QVERIFY(QDir().mkpath(source));
    QVERIFY(createJunction(aliasRoot, actual));

    const QString aliasedSource = QDir(aliasRoot).filePath(QStringLiteral("source"));
    const BackupValidationResult result =
        BackupPrevalidator().validateRoots(source, aliasedSource);
    QVERIFY(!result.valid);
    QCOMPARE(result.code, BackupValidationCode::UnsupportedReparse);
    QVERIFY(result.error.isValid());
    QVERIFY(result.error.context().contains(QStringLiteral("aliasroot")));
#else
    QSKIP("Windows Junction fixture");
#endif
}

void BackupPrevalidatorContractTest::rejectsIntermediateJunctionNestedTarget()
{
#ifdef Q_OS_WIN
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString actual = QDir(directory.path()).filePath(QStringLiteral("actual"));
    const QString source = QDir(actual).filePath(QStringLiteral("source"));
    const QString aliasRoot = QDir(directory.path()).filePath(QStringLiteral("aliasroot"));
    QVERIFY(QDir().mkpath(QDir(source).filePath(QStringLiteral("backup"))));
    QVERIFY(createJunction(aliasRoot, actual));

    const QString nestedDestination =
        QDir(aliasRoot).filePath(QStringLiteral("source/backup"));
    const BackupValidationResult result =
        BackupPrevalidator().validateRoots(source, nestedDestination);
    QVERIFY(!result.valid);
    QCOMPARE(result.code, BackupValidationCode::UnsupportedReparse);
    QVERIFY(result.error.isValid());
#else
    QSKIP("Windows Junction fixture");
#endif
}

void BackupPrevalidatorContractTest::rejectsIntermediateJunctionInSourceChain()
{
#ifdef Q_OS_WIN
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString actual = QDir(directory.path()).filePath(QStringLiteral("actual"));
    const QString source = QDir(actual).filePath(QStringLiteral("source"));
    const QString destination = QDir(actual).filePath(QStringLiteral("destination"));
    const QString aliasRoot = QDir(directory.path()).filePath(QStringLiteral("aliasroot"));
    QVERIFY(QDir().mkpath(source));
    QVERIFY(QDir().mkpath(destination));
    QVERIFY(createJunction(aliasRoot, actual));

    const QString aliasedSource = QDir(aliasRoot).filePath(QStringLiteral("source"));
    const BackupValidationResult result =
        BackupPrevalidator().validateRoots(aliasedSource, destination);
    QVERIFY(!result.valid);
    QCOMPARE(result.code, BackupValidationCode::UnsupportedReparse);
    QVERIFY(result.error.isValid());
#else
    QSKIP("Windows Junction fixture");
#endif
}

void BackupPrevalidatorContractTest::rejectsIntermediateJunctionInDestinationChain()
{
#ifdef Q_OS_WIN
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString actual = QDir(directory.path()).filePath(QStringLiteral("actual"));
    const QString source = QDir(actual).filePath(QStringLiteral("source"));
    const QString destination = QDir(actual).filePath(QStringLiteral("destination"));
    const QString aliasRoot = QDir(directory.path()).filePath(QStringLiteral("aliasroot"));
    QVERIFY(QDir().mkpath(source));
    QVERIFY(QDir().mkpath(destination));
    QVERIFY(createJunction(aliasRoot, actual));

    const QString aliasedDestination =
        QDir(aliasRoot).filePath(QStringLiteral("destination"));
    const BackupValidationResult result =
        BackupPrevalidator().validateRoots(source, aliasedDestination);
    QVERIFY(!result.valid);
    QCOMPARE(result.code, BackupValidationCode::UnsupportedReparse);
    QVERIFY(result.error.isValid());
#else
    QSKIP("Windows Junction fixture");
#endif
}

void BackupPrevalidatorContractTest::rejectsJunctionWithMissingReplacementTarget()
{
#ifdef Q_OS_WIN
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source"));
    const QString target = QDir(directory.path()).filePath(QStringLiteral("target"));
    const QString junction = QDir(directory.path()).filePath(QStringLiteral("aliasroot"));
    QVERIFY(QDir().mkpath(source));
    QVERIFY(QDir().mkpath(target));
    QVERIFY(createJunction(junction, target));
    QVERIFY(QDir().rmdir(target));

    FilesystemPathInfo junctionInfo;
    QString inspectionError;
    QVERIFY(inspectFilesystemPath(junction, junctionInfo, inspectionError));
    QVERIFY(junctionInfo.isReparsePoint());

    const BackupValidationResult result =
        BackupPrevalidator().validateRoots(source, junction);
    QVERIFY(!result.valid);
    QCOMPARE(result.code, BackupValidationCode::UnsupportedReparse);
    QVERIFY(result.error.isValid());
#else
    QSKIP("Windows Junction fixture");
#endif
}

} // namespace BackupTest
} // namespace FilePilot

bool shouldRunBackupTestClass(const char *className)
{
    const QByteArray selected = qgetenv("FILEPILOT_BACKUP_TEST_CLASS");
    return selected.isEmpty() || selected == className;
}

template<typename TestClass>
int runBackupTestClass(const char *className, int argc, char *argv[])
{
    if (!shouldRunBackupTestClass(className)) {
        return 0;
    }
    TestClass test;
    return QTest::qExec(&test, argc, argv);
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    int status = 0;
    status |= runBackupTestClass<FilePilot::BackupTest::FileSnapshotContractTest>(
        "FileSnapshotContractTest", argc, argv);
    status |= runBackupTestClass<FilePilot::BackupTest::FileHasherContractTest>(
        "FileHasherContractTest", argc, argv);
    status |= runBackupTestClass<FilePilot::BackupTest::BackupPlanContractTest>(
        "BackupPlanContractTest", argc, argv);
    status |= runBackupTestClass<FilePilot::BackupTest::BackupInventoryBuilderContractTest>(
        "BackupInventoryBuilderContractTest", argc, argv);
    status |= runBackupTestClass<FilePilot::BackupTest::BackupPrevalidatorContractTest>(
        "BackupPrevalidatorContractTest", argc, argv);
    return status;
}
