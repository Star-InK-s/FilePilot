#include "core/execution/FileOperator.h"

#include "core/filesystem/FileIdentity.h"
#include "core/organize/OrganizePathValidator.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QUuid>

#include <filesystem>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace FilePilot {

namespace {

namespace fs = std::filesystem;

struct FileSnapshot {
    FileIdentity identity;
    qint64 size = -1;
    qint64 modifiedMSecs = 0;
    QByteArray digest;
};

struct PathChainSnapshot {
    std::vector<std::pair<QString, FilesystemPathInfo>> components;
};

QString temporaryDestinationPath(const QString &destination)
{
    return destination + QStringLiteral(".filepilot-part-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QStringList pathComponents(const QString &path)
{
    QStringList components;
    QString current = QFileInfo(path).absoluteFilePath();
    while (true) {
        components.prepend(current);
        const QString parent = QFileInfo(current).absolutePath();
        if (parent == current) {
            break;
        }
        current = parent;
    }
    return components;
}

bool inspectPath(
    const QString &path,
    FilesystemPathInfo &info,
    QString &error)
{
    return inspectFilesystemPath(path, info, error) && info.inspected;
}

bool capturePathChain(
    const QString &path,
    PathChainSnapshot &snapshot,
    QString &error)
{
    snapshot.components.clear();
    for (const QString &component : pathComponents(path)) {
        FilesystemPathInfo info;
        if (!inspectPath(component, info, error)) {
            return false;
        }
        snapshot.components.emplace_back(component, info);
    }
    return true;
}

bool verifyPathChain(
    const PathChainSnapshot &snapshot,
    QString &error)
{
    for (const auto &expected : snapshot.components) {
        FilesystemPathInfo current;
        if (!inspectPath(expected.first, current, error)) {
            return false;
        }
        if (current.kind != expected.second.kind
            || current.identity != expected.second.identity) {
            error = QStringLiteral("路径组件身份或类型已发生变化");
            return false;
        }
        if (current.isReparsePoint()) {
            error = QStringLiteral("路径组件已变为链接或 reparse point");
            return false;
        }
    }
    return true;
}

std::optional<QByteArray> fileDigest(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    constexpr int bufferSize = 1024 * 1024;
    QByteArray buffer(bufferSize, Qt::Uninitialized);
    while (!file.atEnd()) {
        const qint64 bytesRead = file.read(buffer.data(), buffer.size());
        if (bytesRead < 0) {
            return std::nullopt;
        }
        hash.addData(QByteArrayView(buffer.constData(), bytesRead));
    }
    return hash.result();
}

bool inspectFile(
    const QString &path,
    FilesystemPathInfo &info,
    QString &error)
{
    if (!inspectPath(path, info, error)) {
        return false;
    }
    return info.isRegularFile() && info.identity.valid;
}

bool captureFileSnapshot(
    const QString &path,
    const FileIdentity &expectedIdentity,
    FileSnapshot &snapshot,
    QString &error)
{
    FilesystemPathInfo info;
    if (!inspectFile(path, info, error)) {
        if (error.isEmpty()) {
            error = QStringLiteral("文件不存在、不是普通文件或身份不可验证");
        }
        return false;
    }
    if (expectedIdentity.valid && info.identity != expectedIdentity) {
        error = QStringLiteral("文件身份与预期状态不一致");
        return false;
    }

    const std::optional<QByteArray> digest = fileDigest(path);
    if (!digest) {
        error = QStringLiteral("无法读取文件内容生成验证快照");
        return false;
    }

    snapshot.identity = info.identity;
    snapshot.size = info.size;
    snapshot.modifiedMSecs = info.modifiedMSecs;
    snapshot.digest = *digest;
    return true;
}

bool verifyFileSnapshot(
    const QString &path,
    const FileSnapshot &snapshot,
    QString &error)
{
    FileSnapshot current;
    if (!captureFileSnapshot(path, snapshot.identity, current, error)) {
        return false;
    }
    if (current.size != snapshot.size
        || current.digest != snapshot.digest) {
        error = QStringLiteral("文件内容在验证后发生变化");
        return false;
    }
    return true;
}

bool verifyTemporaryMatchesSnapshot(
    const QString &temporaryPath,
    const FileSnapshot &snapshot,
    QString &error)
{
    FileSnapshot temporary;
    if (!captureFileSnapshot(temporaryPath, {}, temporary, error)) {
        return false;
    }
    if (temporary.size != snapshot.size
        || temporary.digest != snapshot.digest) {
        error = QStringLiteral("临时文件内容与已验证源状态不一致");
        return false;
    }
    return true;
}

bool ensureParentDirectory(
    const QString &destination,
    QString &error,
    bool &unsafePath)
{
    unsafePath = false;
    const QString parentPath = QFileInfo(destination).absolutePath();
    for (const QString &path : pathComponents(parentPath)) {
        FilesystemPathInfo info;
        if (!inspectPath(path, info, error)) {
            return false;
        }
        if (info.kind == FilesystemPathKind::Missing) {
            if (!QDir().mkdir(path)) {
                error = QStringLiteral("无法创建目标目录");
                return false;
            }
            if (!inspectPath(path, info, error)) {
                return false;
            }
        }
        if (!info.isDirectory() || info.isReparsePoint()) {
            unsafePath = info.isReparsePoint();
            error = QStringLiteral("目标路径包含链接、reparse point 或非普通目录");
            return false;
        }
    }
    return true;
}

bool moveWithoutReplacement(const QString &source, const QString &destination)
{
#ifdef Q_OS_WIN
    return MoveFileExW(
        reinterpret_cast<LPCWSTR>(source.utf16()),
        reinterpret_cast<LPCWSTR>(destination.utf16()),
        MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    std::error_code code;
    fs::rename(
        fs::u8path(source.toStdString()),
        fs::u8path(destination.toStdString()),
        code);
    return !code;
#endif
}

bool replaceExisting(const QString &source, const QString &destination)
{
#ifdef Q_OS_WIN
    return ReplaceFileW(
        reinterpret_cast<LPCWSTR>(destination.utf16()),
        reinterpret_cast<LPCWSTR>(source.utf16()),
        nullptr,
        REPLACEFILE_WRITE_THROUGH,
        nullptr,
        nullptr) != FALSE;
#else
    std::error_code code;
    fs::rename(
        fs::u8path(source.toStdString()),
        fs::u8path(destination.toStdString()),
        code);
    return !code;
#endif
}

bool sameVolume(const QString &left, const QString &right)
{
#ifdef Q_OS_WIN
    wchar_t leftRoot[MAX_PATH]{};
    wchar_t rightRoot[MAX_PATH]{};
    if (GetVolumePathNameW(
            reinterpret_cast<LPCWSTR>(left.utf16()), leftRoot, MAX_PATH) == FALSE
        || GetVolumePathNameW(
            reinterpret_cast<LPCWSTR>(right.utf16()), rightRoot, MAX_PATH) == FALSE) {
        return false;
    }

    DWORD leftSerial = 0;
    DWORD rightSerial = 0;
    if (GetVolumeInformationW(
            leftRoot, nullptr, 0, &leftSerial, nullptr, nullptr, nullptr, 0) == FALSE
        || GetVolumeInformationW(
            rightRoot, nullptr, 0, &rightSerial, nullptr, nullptr, nullptr, 0) == FALSE) {
        return false;
    }
    return leftSerial == rightSerial;
#else
    std::error_code code;
    return fs::u8path(left.toStdString()).root_path()
        == fs::u8path(right.toStdString()).root_path();
#endif
}

bool copyToTemporary(
    const QString &source,
    const QString &temporary,
    const std::atomic_bool &cancelled,
    QString &error)
{
    QFile sourceFile(source);
    if (!sourceFile.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("无法读取源文件");
        return false;
    }

    QFile temporaryFile(temporary);
    if (!temporaryFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        error = QStringLiteral("无法写入临时目标文件");
        return false;
    }

    constexpr int bufferSize = 1024 * 1024;
    QByteArray buffer(bufferSize, Qt::Uninitialized);
    qint64 totalWritten = 0;
    while (!sourceFile.atEnd()) {
        if (cancelled.load(std::memory_order_relaxed)) {
            error = QStringLiteral("操作已取消");
            return false;
        }

        const qint64 bytesRead = sourceFile.read(buffer.data(), buffer.size());
        if (bytesRead < 0) {
            error = QStringLiteral("读取源文件失败");
            return false;
        }
        if (bytesRead == 0) {
            break;
        }

        const qint64 bytesWritten =
            temporaryFile.write(buffer.constData(), bytesRead);
        if (bytesWritten != bytesRead) {
            error = QStringLiteral("写入临时目标文件失败");
            return false;
        }
        totalWritten += bytesWritten;
    }

    temporaryFile.flush();
    if (totalWritten != QFileInfo(source).size()) {
        error = QStringLiteral("复制完整性校验失败");
        return false;
    }
    return true;
}

bool verifyFileContents(
    const QString &source,
    const QString &temporary,
    QString &error)
{
    QFile sourceFile(source);
    if (!sourceFile.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("无法读取源文件进行发布前验证");
        return false;
    }
    QFile temporaryFile(temporary);
    if (!temporaryFile.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("无法读取临时目标文件进行发布前验证");
        return false;
    }
    if (sourceFile.size() != temporaryFile.size()) {
        error = QStringLiteral("临时目标文件大小校验失败");
        return false;
    }

    constexpr int bufferSize = 1024 * 1024;
    QByteArray sourceBuffer(bufferSize, Qt::Uninitialized);
    QByteArray temporaryBuffer(bufferSize, Qt::Uninitialized);
    while (!sourceFile.atEnd()) {
        const qint64 sourceBytes = sourceFile.read(sourceBuffer.data(), bufferSize);
        const qint64 temporaryBytes =
            temporaryFile.read(temporaryBuffer.data(), bufferSize);
        if (sourceBytes <= 0 || sourceBytes != temporaryBytes
            || sourceBuffer.left(static_cast<int>(sourceBytes))
                != temporaryBuffer.left(static_cast<int>(temporaryBytes))) {
            error = QStringLiteral("临时目标文件内容校验失败");
            return false;
        }
    }
    return temporaryFile.atEnd();
}

bool verifyDestinationForPublish(
    const FileMoveRequest &request,
    const bool overwrite,
    QString &error)
{
    FilesystemPathInfo destinationInfo;
    if (!inspectPath(request.destinationPath, destinationInfo, error)) {
        return false;
    }
    if (overwrite) {
        if (!destinationInfo.isRegularFile()
            || !request.expectedDestinationIdentity.valid
            || destinationInfo.identity != request.expectedDestinationIdentity) {
            error = QStringLiteral("目标文件身份在发布前发生变化");
            return false;
        }
    } else if (destinationInfo.exists()) {
        error = QStringLiteral("目标文件在发布前出现");
        return false;
    }
    return true;
}

#ifdef Q_OS_WIN
bool removeVerifiedSourceWin(
    const QString &source,
    const FileSnapshot &snapshot,
    QString &error)
{
    const QString nativePath = QDir::toNativeSeparators(source);
    HANDLE handle = CreateFileW(
        reinterpret_cast<LPCWSTR>(nativePath.utf16()),
        DELETE | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        error = QStringLiteral("无法锁定已验证源文件进行清理");
        return false;
    }

    BY_HANDLE_FILE_INFORMATION handleInfo{};
    const bool identityMatches =
        GetFileInformationByHandle(handle, &handleInfo) != FALSE
        && handleInfo.dwVolumeSerialNumber == snapshot.identity.volumeSerial
        && ((static_cast<quint64>(handleInfo.nFileIndexHigh) << 32)
            | handleInfo.nFileIndexLow) == snapshot.identity.fileId;
    if (!identityMatches) {
        CloseHandle(handle);
        error = QStringLiteral("源文件身份在清理前发生变化");
        return false;
    }

    FILE_DISPOSITION_INFO disposition{};
    disposition.DeleteFile = TRUE;
    const BOOL marked =
        SetFileInformationByHandle(
            handle,
            FileDispositionInfo,
            &disposition,
            sizeof(disposition));
    CloseHandle(handle);
    if (marked == FALSE) {
        error = QStringLiteral("已验证源文件清理失败");
        return false;
    }
    return true;
}
#endif

bool removeVerifiedSource(
    const QString &source,
    const FileSnapshot &snapshot,
    QString &error)
{
    if (!verifyFileSnapshot(source, snapshot, error)) {
        return false;
    }
#ifdef Q_OS_WIN
    return removeVerifiedSourceWin(source, snapshot, error);
#else
    std::error_code code;
    fs::remove(fs::u8path(source.toStdString()), code);
    if (code) {
        error = QStringLiteral("已验证源文件清理失败");
        return false;
    }
    return true;
#endif
}

std::optional<PublishedMoveRecoveryState> capturePublishedRecoveryState(
    const QString &source,
    const QString &destination,
    const FileIdentity &sourceIdentity,
    const FileIdentity &destinationIdentity)
{
    FileSnapshot sourceSnapshot;
    FileSnapshot destinationSnapshot;
    QString error;
    if (!captureFileSnapshot(source, sourceIdentity, sourceSnapshot, error)
        || !captureFileSnapshot(
              destination, destinationIdentity, destinationSnapshot, error)) {
        return std::nullopt;
    }

    PublishedMoveRecoveryState state;
    state.sourcePath = source;
    state.destinationPath = destination;
    state.sourceIdentity = sourceSnapshot.identity;
    state.destinationIdentity = destinationSnapshot.identity;
    state.sourceSize = sourceSnapshot.size;
    state.destinationSize = destinationSnapshot.size;
    state.sourceDigest = sourceSnapshot.digest;
    state.destinationDigest = destinationSnapshot.digest;
    return state;
}

} // namespace

FileMoveResult FileOperator::resumePublishedCleanup(
    const FileMoveRequest &request,
    const PublishedMoveRecoveryState &recoveryState,
    const std::atomic_bool &cancelled) const
{
    FileMoveResult result;
    result.actualDestination = request.destinationPath;


    if (cancelled.load(std::memory_order_relaxed)) {
        result.status = ExecutionItemStatus::Cancelled;
        result.errorMessage = QStringLiteral("操作已取消");
        return result;
    }
    if (!recoveryState.isValid()
        || !OrganizePathValidator::pathsEqual(
              recoveryState.sourcePath, request.sourcePath)
        || !OrganizePathValidator::pathsEqual(
              recoveryState.destinationPath, request.destinationPath)) {
        result.status = ExecutionItemStatus::Rejected;
        result.errorMessage = QStringLiteral("发布恢复状态与当前计划项不匹配");
        return result;
    }

    FileSnapshot destinationSnapshot;
    QString error;
    if (!captureFileSnapshot(
            request.destinationPath,
            recoveryState.destinationIdentity,
            destinationSnapshot,
            error)
        || destinationSnapshot.size != recoveryState.destinationSize
        || destinationSnapshot.digest != recoveryState.destinationDigest) {
        result.status = ExecutionItemStatus::Rejected;
        result.errorMessage = error.isEmpty()
            ? QStringLiteral("已发布目标文件状态已发生变化")
            : error;
        return result;
    }

    FileSnapshot sourceSnapshot;
    if (!captureFileSnapshot(
            request.sourcePath,
            recoveryState.sourceIdentity,
            sourceSnapshot,
            error)) {
        FilesystemPathInfo sourceInfo;
        QString inspectError;
        if (inspectPath(request.sourcePath, sourceInfo, inspectError)
            && sourceInfo.kind == FilesystemPathKind::Missing) {
            result.status = ExecutionItemStatus::Succeeded;
            result.errorMessage = QStringLiteral("已发布目标文件有效，源文件清理已完成");
            return result;
        }
        result.status = ExecutionItemStatus::Rejected;
        result.errorMessage = error;
        return result;
    }
    if (sourceSnapshot.size != recoveryState.sourceSize
        || sourceSnapshot.digest != recoveryState.sourceDigest) {
        result.status = ExecutionItemStatus::Rejected;
        result.errorMessage = QStringLiteral("源文件与已发布结果不再匹配");
        return result;
    }

    if (cancelled.load(std::memory_order_relaxed)) {
        result.status = ExecutionItemStatus::SourceCleanupFailed;
        result.errorMessage = QStringLiteral("已发布结果有效，但取消导致源清理未执行");
        result.publishedState = recoveryState;
        return result;
    }
    if (!removeVerifiedSource(request.sourcePath, sourceSnapshot, error)) {
        result.status = ExecutionItemStatus::SourceCleanupFailed;
        result.errorMessage = error;
        result.publishedState = recoveryState;
        return result;
    }

    result.status = ExecutionItemStatus::Succeeded;
    result.errorMessage = QStringLiteral("已恢复已发布结果并完成源文件清理");
    result.resumedPublishedResult = true;
    return result;
}

FileMoveResult FileOperator::move(
    const FileMoveRequest &request,
    const std::atomic_bool &cancelled,
    const FileMoveOptions &options) const
{
    FileMoveResult result;
    result.actualDestination = request.destinationPath;

    if (request.action == ConflictDecisionAction::Skip) {
        result.status = ExecutionItemStatus::Skipped;
        result.errorMessage = QStringLiteral("目标文件已存在");
        return result;
    }
    if (request.action == ConflictDecisionAction::Reject) {
        result.status = ExecutionItemStatus::Rejected;
        result.errorMessage = QStringLiteral("计划项已被拒绝");
        return result;
    }
    if (cancelled.load(std::memory_order_relaxed)) {
        result.status = ExecutionItemStatus::Cancelled;
        result.errorMessage = QStringLiteral("操作已取消");
        return result;
    }

    FileSnapshot sourceSnapshot;
    PathChainSnapshot sourceChain;
    PathChainSnapshot destinationChain;
    QString error;
    if (!captureFileSnapshot(
            request.sourcePath, request.expectedSourceIdentity, sourceSnapshot, error)
        || !capturePathChain(request.sourcePath, sourceChain, error)) {
        FilesystemPathInfo sourceProbe;
        QString probeError;
        const bool sourceIsRegular =
            inspectPath(request.sourcePath, sourceProbe, probeError)
            && sourceProbe.isRegularFile();
        const bool expectedIdentityMismatch =
            request.expectedSourceIdentity.valid
            && sourceProbe.identity != request.expectedSourceIdentity;
        result.status = sourceIsRegular && !expectedIdentityMismatch
            ? ExecutionItemStatus::Failed
            : ExecutionItemStatus::Rejected;
        result.errorMessage = error.isEmpty()
            ? QStringLiteral("源文件状态不可验证")
            : error;
        return result;
    }
    if (OrganizePathValidator::pathsEqual(
            request.sourcePath, request.destinationPath)) {
        result.status = ExecutionItemStatus::Rejected;
        result.errorMessage = QStringLiteral("源路径与目标路径相同");
        return result;
    }

    bool unsafeParentPath = false;
    if (!ensureParentDirectory(request.destinationPath, error, unsafeParentPath)) {
        result.status = unsafeParentPath
            ? ExecutionItemStatus::Rejected
            : ExecutionItemStatus::Failed;
        result.errorMessage = error;
        return result;
    }
    if (!capturePathChain(
            QFileInfo(request.destinationPath).absolutePath(),
            destinationChain,
            error)) {
        result.status = ExecutionItemStatus::Rejected;
        result.errorMessage = error;
        return result;
    }

    const bool overwrite =
        request.action == ConflictDecisionAction::Overwrite;
    if (!verifyDestinationForPublish(request, overwrite, error)) {
        result.status = ExecutionItemStatus::Rejected;
        result.errorMessage = error;
        return result;
    }

    if (sameVolume(request.sourcePath, request.destinationPath)) {
        if (cancelled.load(std::memory_order_relaxed)) {
            result.status = ExecutionItemStatus::Cancelled;
            result.errorMessage = QStringLiteral("操作已取消");
            return result;
        }
        if (!verifyFileSnapshot(request.sourcePath, sourceSnapshot, error)
            || !verifyPathChain(sourceChain, error)
            || !verifyPathChain(destinationChain, error)
            || !verifyDestinationForPublish(request, overwrite, error)) {
            result.status = ExecutionItemStatus::Rejected;
            result.errorMessage = error;
            return result;
        }
        if (options.beforePublish
            && !options.beforePublish(
                request.sourcePath, request.destinationPath, error)) {
            result.status = ExecutionItemStatus::Failed;
            result.errorMessage = error;
            return result;
        }
        if (cancelled.load(std::memory_order_relaxed)) {
            result.status = ExecutionItemStatus::Cancelled;
            result.errorMessage = QStringLiteral("操作已取消");
            return result;
        }
        if (!verifyFileSnapshot(request.sourcePath, sourceSnapshot, error)
            || !verifyPathChain(sourceChain, error)
            || !verifyPathChain(destinationChain, error)
            || !verifyDestinationForPublish(request, overwrite, error)) {
            result.status = ExecutionItemStatus::Rejected;
            result.errorMessage = error;
            return result;
        }

        const bool moved = overwrite
            ? replaceExisting(request.sourcePath, request.destinationPath)
            : moveWithoutReplacement(request.sourcePath, request.destinationPath);
        if (!moved) {
            result.status = ExecutionItemStatus::Failed;
            result.errorMessage = QStringLiteral("移动文件失败");
            return result;
        }

        FileSnapshot publishedSnapshot;
        if (!captureFileSnapshot(
                request.destinationPath,
                {},
                publishedSnapshot,
                error)
            || publishedSnapshot.size != sourceSnapshot.size
            || publishedSnapshot.digest != sourceSnapshot.digest) {
            result.status = ExecutionItemStatus::SourceCleanupFailed;
            result.errorMessage = QStringLiteral("目标文件已发布，但最终状态确认失败");
            result.publishedState = capturePublishedRecoveryState(
                request.sourcePath,
                request.destinationPath,
                sourceSnapshot.identity,
                publishedSnapshot.identity);
            return result;
        }
        result.status = ExecutionItemStatus::Succeeded;
        return result;
    }

    const QString temporaryPath =
        temporaryDestinationPath(request.destinationPath);
    QString copyError;
    if (!copyToTemporary(
            request.sourcePath, temporaryPath, cancelled, copyError)) {
        QFile::remove(temporaryPath);
        result.status = cancelled.load(std::memory_order_relaxed)
            ? ExecutionItemStatus::Cancelled
            : ExecutionItemStatus::Failed;
        result.errorMessage = copyError;
        return result;
    }

    if (options.temporaryFileVerifier
        && !options.temporaryFileVerifier(
              request.sourcePath, temporaryPath, error)) {
        QFile::remove(temporaryPath);
        result.status = ExecutionItemStatus::Failed;
        result.errorMessage = error;
        return result;
    }
    if (!options.temporaryFileVerifier
        && !verifyFileContents(request.sourcePath, temporaryPath, error)) {
        QFile::remove(temporaryPath);
        result.status = ExecutionItemStatus::Failed;
        result.errorMessage = error;
        return result;
    }

    FileSnapshot verifiedSource;
    if (!captureFileSnapshot(
            request.sourcePath,
            request.expectedSourceIdentity,
            verifiedSource,
            error)
        || !verifyTemporaryMatchesSnapshot(temporaryPath, verifiedSource, error)
        || !verifyFileSnapshot(request.sourcePath, sourceSnapshot, error)
        || !verifyPathChain(sourceChain, error)
        || !verifyPathChain(destinationChain, error)
        || !verifyDestinationForPublish(request, overwrite, error)) {
        QFile::remove(temporaryPath);
        result.status = ExecutionItemStatus::Rejected;
        result.errorMessage = error;
        return result;
    }

    if (options.beforePublish
        && !options.beforePublish(
            request.sourcePath, request.destinationPath, error)) {
        QFile::remove(temporaryPath);
        result.status = ExecutionItemStatus::Failed;
        result.errorMessage = error;
        return result;
    }
    if (cancelled.load(std::memory_order_relaxed)) {
        QFile::remove(temporaryPath);
        result.status = ExecutionItemStatus::Cancelled;
        result.errorMessage = QStringLiteral("操作已取消");
        return result;
    }
    if (!verifyFileSnapshot(request.sourcePath, verifiedSource, error)
        || !verifyTemporaryMatchesSnapshot(temporaryPath, verifiedSource, error)
        || !verifyPathChain(sourceChain, error)
        || !verifyPathChain(destinationChain, error)
        || !verifyDestinationForPublish(request, overwrite, error)) {
        QFile::remove(temporaryPath);
        result.status = ExecutionItemStatus::Rejected;
        result.errorMessage = error;
        return result;
    }

    const bool published = overwrite
        ? replaceExisting(temporaryPath, request.destinationPath)
        : moveWithoutReplacement(temporaryPath, request.destinationPath);
    if (!published) {
        QFile::remove(temporaryPath);
        result.status = ExecutionItemStatus::Failed;
        result.errorMessage = QStringLiteral("目标文件写入失败");
        return result;
    }

    FileSnapshot publishedSnapshot;
    if (!captureFileSnapshot(
            request.destinationPath,
            {},
            publishedSnapshot,
            error)
        || publishedSnapshot.size != verifiedSource.size
        || publishedSnapshot.digest != verifiedSource.digest) {
        result.status = ExecutionItemStatus::SourceCleanupFailed;
        result.errorMessage = QStringLiteral("目标文件已发布，但最终状态确认失败，源清理未执行");
        result.publishedState = capturePublishedRecoveryState(
            request.sourcePath,
            request.destinationPath,
            verifiedSource.identity,
            publishedSnapshot.identity);
        return result;
    }

    if (cancelled.load(std::memory_order_relaxed)) {
        result.status = ExecutionItemStatus::SourceCleanupFailed;
        result.errorMessage = QStringLiteral("目标文件已发布，但取消导致源清理未执行");
        result.publishedState = capturePublishedRecoveryState(
            request.sourcePath,
            request.destinationPath,
            verifiedSource.identity,
            publishedSnapshot.identity);
        return result;
    }
    if (!removeVerifiedSource(request.sourcePath, verifiedSource, error)) {
        result.status = ExecutionItemStatus::SourceCleanupFailed;
        result.errorMessage = error;
        result.publishedState = capturePublishedRecoveryState(
            request.sourcePath,
            request.destinationPath,
            verifiedSource.identity,
            publishedSnapshot.identity);
        return result;
    }

    result.status = ExecutionItemStatus::Succeeded;
    return result;
}

} // namespace FilePilot
