#include "core/backup/BackupInventoryBuilder.h"

#include "core/filesystem/FileSnapshot.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <utility>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace FilePilot {
namespace {

bool enumerateChildren(const QString &directory,
                       QStringList &children,
                       AppError &error)
{
    children.clear();

#ifdef Q_OS_WIN
    const QString pattern = QDir::toNativeSeparators(
        QDir(directory).filePath(QStringLiteral("*")));
    WIN32_FIND_DATAW entry{};
    HANDLE handle = FindFirstFileW(
        reinterpret_cast<LPCWSTR>(pattern.utf16()),
        &entry);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_NO_MORE_FILES) {
            return true;
        }
        error = AppError{
            code == ERROR_ACCESS_DENIED ? ErrorCode::AccessDenied : ErrorCode::Io,
            QStringLiteral("无法完整枚举备份目录"),
            directory,
        };
        return false;
    }

    do {
        const QString name = QString::fromWCharArray(entry.cFileName);
        if (name == QStringLiteral(".") || name == QStringLiteral("..")) {
            continue;
        }
        children.append(QDir(directory).filePath(name));
    } while (FindNextFileW(handle, &entry) != FALSE);

    const DWORD finalError = GetLastError();
    FindClose(handle);
    if (finalError != ERROR_NO_MORE_FILES) {
        error = AppError{
            finalError == ERROR_ACCESS_DENIED ? ErrorCode::AccessDenied
                                              : ErrorCode::Io,
            QStringLiteral("无法完整枚举备份目录"),
            directory,
        };
        return false;
    }
#else
    const std::filesystem::path nativeDirectory =
        std::filesystem::u8path(directory.toStdString());
    std::error_code code;
    std::filesystem::directory_iterator iterator(
        nativeDirectory,
        std::filesystem::directory_options::none,
        code);
    if (code) {
        error = AppError{
            code == std::errc::permission_denied ? ErrorCode::AccessDenied
                                                  : ErrorCode::Io,
            QStringLiteral("无法完整枚举备份目录"),
            directory,
        };
        return false;
    }

    const std::filesystem::directory_iterator end;
    while (iterator != end) {
        children.append(QString::fromStdString(iterator->path().u8string()));
        iterator.increment(code);
        if (code) {
            error = AppError{
                code == std::errc::permission_denied ? ErrorCode::AccessDenied
                                                      : ErrorCode::Io,
                QStringLiteral("无法完整枚举备份目录"),
                directory,
            };
            return false;
        }
    }
#endif

    std::sort(children.begin(), children.end(), [](const QString &left,
                                                   const QString &right) {
        const int comparison = left.compare(right, Qt::CaseInsensitive);
        return comparison != 0 ? comparison < 0 : left < right;
    });
    return true;
}

bool inspectEntry(const QString &path, FilesystemPathInfo &info, AppError &error)
{
    QString inspectionError;
    if (!inspectFilesystemPath(path, info, inspectionError) || !info.inspected) {
        error = AppError{
            ErrorCode::Io,
            inspectionError.isEmpty()
                ? QStringLiteral("无法检查备份 inventory 项")
                : inspectionError,
            path,
        };
        return false;
    }
    return true;
}

bool appendInventoryEntry(const QString &root,
                          const QString &path,
                          BackupInventory &inventory,
                          AppError &error,
                          const QString &relativePathOverride = QString())
{
    FilesystemPathInfo info;
    if (!inspectEntry(path, info, error)) {
        if (QFileInfo(path).isDir()) {
            QStringList probeChildren;
            AppError enumerationError;
            if (!enumerateChildren(path, probeChildren, enumerationError)) {
                error = enumerationError;
            }
        }
        return false;
    }

    const QString relativePath = relativePathOverride.isEmpty()
        ? QDir(root).relativeFilePath(path)
        : relativePathOverride;
    if (info.isReparsePoint()) {
        inventory.unsupportedEntries.push_back(BackupUnsupportedEntry{
            relativePath,
            path,
            info.kind,
            QStringLiteral("Phase 1 不支持 Junction、symbolic link 或 reparse point"),
        });
        return true;
    }

    if (!info.isRegularFile() && !info.isDirectory()) {
        inventory.unsupportedEntries.push_back(BackupUnsupportedEntry{
            relativePath,
            path,
            info.kind,
            QStringLiteral("不支持的文件系统对象类型"),
        });
        return true;
    }

    FileSnapshot snapshot;
    if (!captureFileSnapshot(path, snapshot, error)) {
        return false;
    }

    BackupPlanItem item;
    item.relativePath = relativePath;
    item.sourcePath = path;
    item.kind = info.isDirectory() ? BackupEntryKind::Directory
                                   : BackupEntryKind::File;
    item.expectedSourceIdentity = snapshot.identity;
    item.expectedSize = snapshot.size;
    item.expectedModifiedTime = snapshot.modifiedTime;
    item.sourceSnapshot = snapshot;
    item.plannedStatus = BackupPlanItemStatus::Ready;
    inventory.items.push_back(std::move(item));

    if (!info.isDirectory()) {
        return true;
    }

    QStringList children;
    if (!enumerateChildren(path, children, error)) {
        return false;
    }
    for (const QString &child : children) {
        if (!appendInventoryEntry(root, child, inventory, error)) {
            return false;
        }
    }
    return true;
}

void finalizeCounts(BackupInventory &inventory)
{
    inventory.expectedFileCount = 0;
    inventory.expectedDirectoryCount = 0;
    inventory.expectedBytes = 0;
    for (const BackupPlanItem &item : inventory.items) {
        if (item.kind == BackupEntryKind::Directory) {
            ++inventory.expectedDirectoryCount;
        } else {
            ++inventory.expectedFileCount;
            if (item.expectedSize > 0) {
                inventory.expectedBytes += item.expectedSize;
            }
        }
    }

    std::sort(
        inventory.items.begin(),
        inventory.items.end(),
        [](const BackupPlanItem &left, const BackupPlanItem &right) {
            const int comparison =
                left.relativePath.compare(right.relativePath, Qt::CaseInsensitive);
            return comparison != 0 ? comparison < 0
                                   : left.relativePath < right.relativePath;
        });
    std::sort(
        inventory.unsupportedEntries.begin(),
        inventory.unsupportedEntries.end(),
        [](const BackupUnsupportedEntry &left,
           const BackupUnsupportedEntry &right) {
            const int comparison =
                left.relativePath.compare(right.relativePath, Qt::CaseInsensitive);
            return comparison != 0 ? comparison < 0
                                   : left.relativePath < right.relativePath;
        });
}

} // namespace

bool BackupInventoryBuilder::build(const QString &sourceRoot,
                                   BackupInventory &inventory,
                                   AppError &error) const
{
    inventory = BackupInventory{};
    error = AppError{};

    if (sourceRoot.trimmed().isEmpty()) {
        error = AppError{
            ErrorCode::InvalidPath,
            QStringLiteral("备份源路径不能为空"),
            sourceRoot,
        };
        return false;
    }

    FileSnapshot rootSnapshot;
    AppError snapshotError;
    const bool rootCaptured =
        captureFileSnapshot(sourceRoot, rootSnapshot, snapshotError);
    const QFileInfo rootInfo(sourceRoot);
    if (!rootCaptured) {
        if (!rootInfo.exists()) {
            error = AppError{
                ErrorCode::NotFound,
                QStringLiteral("备份源路径不存在"),
                sourceRoot,
            };
            return false;
        }
        if (rootInfo.isDir()) {
            QStringList probeChildren;
            if (!enumerateChildren(sourceRoot, probeChildren, error)) {
                return false;
            }
        }
        error = snapshotError;
        return false;
    }

    BackupInventory built;
    built.sourceRoot = sourceRoot;
    built.sourceRootSnapshot = rootSnapshot;

    if (rootSnapshot.reparsePoint
        || rootSnapshot.kind == FilesystemPathKind::ReparsePoint) {
        error = AppError{
            ErrorCode::InvalidPath,
            QStringLiteral("备份源根路径不能是 Junction、symbolic link 或 reparse point"),
            sourceRoot,
        };
        return false;
    }

    if (rootSnapshot.kind == FilesystemPathKind::RegularFile) {
        if (!appendInventoryEntry(
                sourceRoot,
                sourceRoot,
                built,
                error,
                rootInfo.fileName())) {
            return false;
        }
    } else if (rootSnapshot.kind == FilesystemPathKind::Directory) {
        QStringList children;
        if (!enumerateChildren(sourceRoot, children, error)) {
            return false;
        }
        for (const QString &child : children) {
            if (!appendInventoryEntry(sourceRoot, child, built, error)) {
                return false;
            }
        }
    } else {
        error = AppError{
            ErrorCode::InvalidPath,
            QStringLiteral("备份源必须是普通文件或目录"),
            sourceRoot,
        };
        return false;
    }

    finalizeCounts(built);
    inventory = std::move(built);
    return true;
}

} // namespace FilePilot
