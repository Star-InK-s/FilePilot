#include "core/organize/OrganizeExecutionPrevalidator.h"

#include "core/organize/OrganizePathValidator.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>

#include <cmath>
#include <filesystem>
#include <limits>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace FilePilot {

namespace {

namespace fs = std::filesystem;

struct NativePathInfo {
    bool exists = false;
    bool directory = false;
    bool regularFile = false;
    bool reparsePoint = false;
    quint32 volumeSerial = 0;
    quint64 fileId = 0;
    std::error_code error;
};

#ifdef Q_OS_WIN
NativePathInfo inspectNativePath(const QString &path)
{
    NativePathInfo info;

    WIN32_FIND_DATAW findData{};
    HANDLE findHandle = FindFirstFileW(
        reinterpret_cast<LPCWSTR>(path.utf16()), &findData);
    if (findHandle == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();
        info.error = std::error_code(static_cast<int>(code), std::system_category());
        return info;
    }
    FindClose(findHandle);

    info.exists = true;
    info.directory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    info.reparsePoint =
        (findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    info.regularFile = !info.directory && !info.reparsePoint;

    HANDLE fileHandle = CreateFileW(
        reinterpret_cast<LPCWSTR>(path.utf16()),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);
    if (fileHandle == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();
        info.error = std::error_code(static_cast<int>(code), std::system_category());
        return info;
    }

    BY_HANDLE_FILE_INFORMATION handleInfo{};
    if (GetFileInformationByHandle(fileHandle, &handleInfo) != FALSE) {
        info.volumeSerial = handleInfo.dwVolumeSerialNumber;
        info.fileId = (static_cast<quint64>(handleInfo.nFileIndexHigh) << 32)
            | handleInfo.nFileIndexLow;
    } else {
        const DWORD code = GetLastError();
        info.error = std::error_code(static_cast<int>(code), std::system_category());
    }

    CloseHandle(fileHandle);
    return info;
}
#else
NativePathInfo inspectNativePath(const QString &path)
{
    NativePathInfo info;
    std::error_code error;
    const fs::file_status status =
        fs::symlink_status(fs::u8path(path.toStdString()), error);
    info.error = error;
    if (error) {
        return info;
    }

    info.exists = true;
    info.directory = fs::is_directory(status);
    info.regularFile = fs::is_regular_file(status);
    info.reparsePoint = fs::is_symlink(status);
    return info;
}
#endif

bool sameTimestamp(const QDateTime &left, const QDateTime &right)
{
    if (!left.isValid() || !right.isValid()) {
        return false;
    }

    return std::llabs(left.toMSecsSinceEpoch() - right.toMSecsSinceEpoch()) <= 1000;
}

QString normalizedComparisonPath(const QString &path)
{
    return OrganizePathValidator::normalizePath(path);
}

} // namespace

ExecutionValidationResult OrganizeExecutionPrevalidator::validateTargetRoot(
    const OrganizePlanProvenance &provenance) const
{
    if (provenance.normalizedTargetRoot.isEmpty()
        || provenance.targetRootKind == TargetRootKind::Empty
        || provenance.planGeneration == 0
        || provenance.scanGeneration == 0
        || provenance.scanSourceRoot.isEmpty()) {
        return {false, QStringLiteral("计划来源信息不完整")};
    }

    const QFileInfo rootInfo(provenance.normalizedTargetRoot);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        return {false, QStringLiteral("目标根目录不存在或不是目录")};
    }

    const NativePathInfo nativeRoot =
        inspectNativePath(provenance.normalizedTargetRoot);
    if (nativeRoot.error) {
        return {false, QStringLiteral("无法读取目标根目录身份")};
    }
    if (!nativeRoot.directory || nativeRoot.reparsePoint) {
        return {false, QStringLiteral("目标根目录不能是链接或 reparse point")};
    }

    QString expectedRoot = normalizedComparisonPath(
        provenance.normalizedTargetRoot);
    if (provenance.targetRootKind == TargetRootKind::Relative) {
        expectedRoot = normalizedComparisonPath(rootInfo.absoluteFilePath());
    }
    if (!OrganizePathValidator::pathsEqual(
            expectedRoot, rootInfo.absoluteFilePath())) {
        return {false, QStringLiteral("目标根目录路径已发生变化")};
    }

    if (!targetRootCaptured_) {
        targetRootSnapshot_.valid = true;
        targetRootSnapshot_.volumeSerial = nativeRoot.volumeSerial;
        targetRootSnapshot_.fileId = nativeRoot.fileId;
        targetRootCaptured_ = true;
    } else if (targetRootSnapshot_.volumeSerial != nativeRoot.volumeSerial
        || targetRootSnapshot_.fileId != nativeRoot.fileId) {
        return {false, QStringLiteral("目标根目录身份或卷已发生变化")};
    }

    return {true, QString()};
}

ExecutionValidationResult OrganizeExecutionPrevalidator::validateCandidate(
    const OrganizePlanProvenance &provenance,
    const OrganizePlanItem &item) const
{
    const ExecutionValidationResult rootResult =
        validateTargetRoot(provenance);
    if (!rootResult.valid) {
        return rootResult;
    }

    if (item.planStatus != OrganizePlanStatus::Planned) {
        return {false, QStringLiteral("该计划项不是可执行候选")};
    }
    if (item.sourcePath.isEmpty() || item.destinationPath.isEmpty()) {
        return {false, QStringLiteral("计划项缺少源路径或目标路径")};
    }

    if (OrganizePathValidator::pathsEqual(
            item.sourcePath, item.destinationPath)) {
        return {false, QStringLiteral("源路径与目标路径相同")};
    }

    const QFileInfo sourceInfo(item.sourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        return {false, QStringLiteral("源文件不存在或不是普通文件")};
    }

    const NativePathInfo nativeSource = inspectNativePath(item.sourcePath);
    if (nativeSource.error) {
        return {false, QStringLiteral("无法读取源文件身份")};
    }
    if (!nativeSource.regularFile || nativeSource.reparsePoint) {
        return {false, QStringLiteral("源文件不是普通文件或包含 reparse point")};
    }
    if (sourceInfo.size() != item.fileSize) {
        return {false, QStringLiteral("源文件大小已发生变化")};
    }
    if (!sameTimestamp(sourceInfo.lastModified().toUTC(), item.modifiedTime)) {
        return {false, QStringLiteral("源文件修改时间已发生变化")};
    }

    if (!OrganizePathValidator::isPathInsideRoot(
            provenance.normalizedTargetRoot, item.destinationPath)) {
        return {false, QStringLiteral("目标路径超出目标根目录")};
    }

    if (!OrganizePathValidator::pathsEqual(
            sourceInfo.canonicalFilePath(), item.sourcePath)) {
        return {false, QStringLiteral("源文件路径身份已发生变化")};
    }

    ExecutionValidationResult result{true, QString()};
#ifdef Q_OS_WIN
    result.sourceIdentity.valid = true;
    result.sourceIdentity.volumeSerial = nativeSource.volumeSerial;
    result.sourceIdentity.fileId = nativeSource.fileId;
#endif
    return result;
}

} // namespace FilePilot
