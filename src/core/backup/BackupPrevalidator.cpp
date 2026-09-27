#include "core/backup/BackupPrevalidator.h"

#include "core/filesystem/FileIdentity.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStorageInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace FilePilot {
namespace {

constexpr qsizetype maximumBackupPathLength = 4096;

BackupValidationResult failure(
    const BackupValidationCode code,
    const ErrorCode errorCode,
    const QString &message,
    const QString &path)
{
    BackupValidationResult result;
    result.valid = false;
    result.code = code;
    result.error = AppError{errorCode, message, path};
    return result;
}

bool isReservedWindowsName(const QString &component)
{
    const QString base = component.split(QLatin1Char('.')).first().toUpper();
    if (base == QStringLiteral("CON")
        || base == QStringLiteral("PRN")
        || base == QStringLiteral("AUX")
        || base == QStringLiteral("NUL")) {
        return true;
    }

    static const QRegularExpression reservedDevice(
        QStringLiteral("^(COM|LPT)[1-9]$"));
    return reservedDevice.match(base).hasMatch();
}

bool validatePathSyntax(const QString &path)
{
    if (path.trimmed().isEmpty() || path.size() > maximumBackupPathLength) {
        return false;
    }

    const QString normalized = QDir::fromNativeSeparators(path);
    if (normalized.startsWith(QStringLiteral("//"))) {
        return false;
    }
    if (!QDir::isAbsolutePath(normalized)) {
        return false;
    }

    const qsizetype drivePrefixLength =
        normalized.size() >= 2 && normalized.at(1) == QLatin1Char(':') ? 2 : 0;
    for (qsizetype index = 0; index < normalized.size(); ++index) {
        const QChar character = normalized.at(index);
        const ushort value = character.unicode();
        if (value < 0x20) {
            return false;
        }
        if (index >= drivePrefixLength
            && (character == QLatin1Char('<')
                || character == QLatin1Char('>')
                || character == QLatin1Char(':')
                || character == QLatin1Char('"')
                || character == QLatin1Char('|')
                || character == QLatin1Char('?')
                || character == QLatin1Char('*'))) {
            return false;
        }
    }

    QString withoutRoot = normalized.mid(drivePrefixLength);
    const QStringList components = withoutRoot.split(
        QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &component : components) {
        if (component.endsWith(QLatin1Char('.'))
            || component.endsWith(QLatin1Char(' '))
            || isReservedWindowsName(component)) {
            return false;
        }
    }
    return true;
}

QString normalizedRoot(const QString &path)
{
    QString normalized = QDir::cleanPath(QDir::fromNativeSeparators(path));
    while (normalized.size() > 3 && normalized.endsWith(QLatin1Char('/'))) {
        normalized.chop(1);
    }
    return normalized;
}

bool pathsEqual(const QString &left, const QString &right)
{
    return normalizedRoot(left).toCaseFolded()
        == normalizedRoot(right).toCaseFolded();
}

bool pathContains(const QString &parent, const QString &child)
{
    const QString normalizedParent = normalizedRoot(parent).toCaseFolded();
    const QString normalizedChild = normalizedRoot(child).toCaseFolded();
    if (normalizedParent.isEmpty() || normalizedChild.isEmpty()) {
        return false;
    }
    if (normalizedParent == normalizedChild) {
        return true;
    }

    QString prefix = normalizedParent;
    if (!prefix.endsWith(QLatin1Char('/'))) {
        prefix += QLatin1Char('/');
    }
    return normalizedChild.startsWith(prefix);
}

bool inspectRoot(const QString &path, FilesystemPathInfo &info, AppError &error)
{
    QString inspectionError;
    if (!inspectFilesystemPath(path, info, inspectionError) || !info.inspected) {
        error = AppError{
            ErrorCode::Io,
            inspectionError.isEmpty()
                ? QStringLiteral("无法验证备份根路径")
                : inspectionError,
            path,
        };
        return false;
    }
    return true;
}

enum class PathChainState {
    Safe,
    ReparsePoint,
    Error
};

PathChainState inspectPathChain(const QString &path,
                                QString &offendingPath,
                                AppError &error)
{
    offendingPath.clear();
    QString current = normalizedRoot(path);
    while (true) {
        FilesystemPathInfo info;
        QString inspectionError;
        if (!inspectFilesystemPath(current, info, inspectionError)
            || !info.inspected) {
            error = AppError{
                ErrorCode::Io,
                inspectionError.isEmpty()
                    ? QStringLiteral("无法验证备份路径组件链")
                    : inspectionError,
                current,
            };
            return PathChainState::Error;
        }
        if (info.isReparsePoint()) {
            offendingPath = current;
            return PathChainState::ReparsePoint;
        }

        const QString parent = QFileInfo(current).absolutePath();
        if (parent == current) {
            return PathChainState::Safe;
        }
        current = parent;
    }
}

bool relationshipUnsafe(const QString &source,
                        const QString &destination,
                        const FilesystemPathInfo &sourceInfo,
                        const FilesystemPathInfo &destinationInfo)
{
    if (sourceInfo.identity.valid
        && destinationInfo.identity.valid
        && sourceInfo.identity == destinationInfo.identity) {
        return true;
    }
    if (pathsEqual(source, destination)
        || pathContains(source, destination)
        || pathContains(destination, source)) {
        return true;
    }

    const QString canonicalSource = QFileInfo(source).canonicalFilePath();
    const QString canonicalDestination =
        QFileInfo(destination).canonicalFilePath();
    if (!canonicalSource.isEmpty() && !canonicalDestination.isEmpty()) {
        return pathsEqual(canonicalSource, canonicalDestination)
            || pathContains(canonicalSource, canonicalDestination)
            || pathContains(canonicalDestination, canonicalSource);
    }
    return false;
}

bool captureVolume(const QString &path,
                   BackupVolumeInfo &volume,
                   AppError &error)
{
    QString probePath = normalizedRoot(path);
    while (true) {
        FilesystemPathInfo info;
        if (inspectRoot(probePath, info, error) && info.exists()) {
            break;
        }
        const QFileInfo pathInfo(probePath);
        const QString parent = pathInfo.absolutePath();
        if (parent == probePath) {
            error = AppError{
                ErrorCode::Io,
                QStringLiteral("无法定位可用的备份卷"),
                path,
            };
            return false;
        }
        probePath = parent;
    }

#ifdef Q_OS_WIN
    const QString nativeProbe = QDir::toNativeSeparators(probePath);
    wchar_t volumeRoot[MAX_PATH]{};
    if (GetVolumePathNameW(
            reinterpret_cast<LPCWSTR>(nativeProbe.utf16()),
            volumeRoot,
            MAX_PATH)
        == FALSE) {
        error = AppError{
            ErrorCode::Io,
            QStringLiteral("无法解析备份卷根路径"),
            path,
        };
        return false;
    }

    DWORD serial = 0;
    if (GetVolumeInformationW(
            volumeRoot,
            nullptr,
            0,
            &serial,
            nullptr,
            nullptr,
            nullptr,
            0)
        == FALSE) {
        error = AppError{
            ErrorCode::Io,
            QStringLiteral("无法读取备份卷身份"),
            path,
        };
        return false;
    }

    volume.valid = serial != 0;
    volume.serial = static_cast<quint32>(serial);
    volume.rootPath = QString::fromWCharArray(volumeRoot);
#else
    const QStorageInfo storage(probePath);
    volume.valid = storage.isValid() && storage.isReady();
    volume.serial = static_cast<quint32>(storage.serialNumber());
    volume.rootPath = storage.rootPath();
#endif

    if (!volume.valid || volume.rootPath.isEmpty()) {
        error = AppError{
            ErrorCode::Io,
            QStringLiteral("备份卷不可用"),
            path,
        };
        return false;
    }
    return true;
}

bool destinationCanBeCreated(const QString &destination)
{
    if (QFileInfo::exists(destination)) {
        return true;
    }

    const QFileInfo parentInfo(QFileInfo(destination).absolutePath());
    return parentInfo.exists() && parentInfo.isDir() && !parentInfo.isSymLink()
        && parentInfo.isWritable();
}

} // namespace

BackupValidationResult BackupPrevalidator::validateRoots(
    const QString &source,
    const QString &destination) const
{
    if (!validatePathSyntax(source) || !validatePathSyntax(destination)) {
        return failure(
            BackupValidationCode::InvalidPath,
            ErrorCode::InvalidPath,
            QStringLiteral("备份路径格式无效或超出支持长度"),
            source);
    }

    QString offendingPath;
    AppError chainError;

    switch (inspectPathChain(source, offendingPath, chainError)) {
    case PathChainState::Safe:
        break;
    case PathChainState::ReparsePoint:
        return failure(
            BackupValidationCode::UnsupportedReparse,
            ErrorCode::InvalidPath,
            QStringLiteral("备份路径组件链包含 Junction、symbolic link 或 reparse point"),
            offendingPath);
    case PathChainState::Error: {
        BackupValidationResult result;
        result.valid = false;
        result.code = BackupValidationCode::SnapshotFailed;
        result.error = chainError;
        return result;
    }
    }

    switch (inspectPathChain(destination, offendingPath, chainError)) {
    case PathChainState::Safe:
        break;
    case PathChainState::ReparsePoint:
        return failure(
            BackupValidationCode::UnsupportedReparse,
            ErrorCode::InvalidPath,
            QStringLiteral("备份路径组件链包含 Junction、symbolic link 或 reparse point"),
            offendingPath);
    case PathChainState::Error: {
        BackupValidationResult result;
        result.valid = false;
        result.code = BackupValidationCode::SnapshotFailed;
        result.error = chainError;
        return result;
    }
    }

    FilesystemPathInfo sourceInfo;
    AppError sourceInspectionError;
    if (!inspectRoot(source, sourceInfo, sourceInspectionError)) {
        BackupValidationResult result;
        result.valid = false;
        result.code = BackupValidationCode::SnapshotFailed;
        result.error = sourceInspectionError;
        return result;
    }
    if (sourceInfo.kind == FilesystemPathKind::Missing) {
        return failure(
            BackupValidationCode::MissingSource,
            ErrorCode::NotFound,
            QStringLiteral("备份源路径不存在"),
            source);
    }
    if (sourceInfo.isReparsePoint()) {
        return failure(
            BackupValidationCode::UnsupportedReparse,
            ErrorCode::InvalidPath,
            QStringLiteral("备份源根路径不能是 Junction、symbolic link 或 reparse point"),
            source);
    }
    if (!sourceInfo.isRegularFile() && !sourceInfo.isDirectory()) {
        return failure(
            BackupValidationCode::InvalidPath,
            ErrorCode::InvalidPath,
            QStringLiteral("备份源必须是普通文件或目录"),
            source);
    }

    FilesystemPathInfo destinationInfo;
    AppError destinationInspectionError;
    if (!inspectRoot(destination, destinationInfo, destinationInspectionError)) {
        BackupValidationResult result;
        result.valid = false;
        result.code = BackupValidationCode::SnapshotFailed;
        result.error = destinationInspectionError;
        return result;
    }
    if (destinationInfo.isReparsePoint()) {
        return failure(
            BackupValidationCode::UnsupportedReparse,
            ErrorCode::InvalidPath,
            QStringLiteral("备份目标根路径不能是 Junction、symbolic link 或 reparse point"),
            destination);
    }

    if (relationshipUnsafe(
            source, destination, sourceInfo, destinationInfo)) {
        return failure(
            BackupValidationCode::PathRelationshipInvalid,
            ErrorCode::InvalidPath,
            QStringLiteral("备份源与备份目标不能相同、指向同一对象或互相嵌套"),
            destination);
    }

    if (destinationInfo.kind == FilesystemPathKind::Missing) {
        if (!destinationCanBeCreated(destination)) {
            return failure(
                BackupValidationCode::DestinationNotCreatable,
                ErrorCode::InvalidPath,
                QStringLiteral("备份目标路径当前不可创建"),
                destination);
        }
    } else if (!destinationInfo.isRegularFile() && !destinationInfo.isDirectory()) {
        return failure(
            BackupValidationCode::InvalidPath,
            ErrorCode::InvalidPath,
            QStringLiteral("备份目标必须是普通文件或目录"),
            destination);
    }

    BackupVolumeInfo sourceVolume;
    BackupVolumeInfo destinationVolume;
    AppError volumeError;
    if (!captureVolume(source, sourceVolume, volumeError)
        || !captureVolume(destination, destinationVolume, volumeError)) {
        BackupValidationResult result;
        result.valid = false;
        result.code = BackupValidationCode::VolumeUnavailable;
        result.error = volumeError;
        return result;
    }

    FileSnapshot sourceSnapshot;
    FileSnapshot destinationSnapshot;
    AppError snapshotError;
    if (!captureFileSnapshot(source, sourceSnapshot, snapshotError)) {
        BackupValidationResult result;
        result.valid = false;
        result.code = BackupValidationCode::SnapshotFailed;
        result.error = snapshotError;
        return result;
    }

    if (destinationInfo.kind != FilesystemPathKind::Missing) {
        if (!captureFileSnapshot(destination, destinationSnapshot, snapshotError)) {
            BackupValidationResult result;
            result.valid = false;
            result.code = BackupValidationCode::SnapshotFailed;
            result.error = snapshotError;
            return result;
        }
    } else {
        destinationSnapshot.path = destination;
        destinationSnapshot.kind = FilesystemPathKind::Missing;
    }

    BackupValidationResult result;
    result.valid = true;
    result.code = BackupValidationCode::Valid;
    result.sourceRootSnapshot = sourceSnapshot;
    result.destinationRootSnapshot = destinationSnapshot;
    result.sourceVolume = sourceVolume;
    result.destinationVolume = destinationVolume;
    return result;
}

} // namespace FilePilot
