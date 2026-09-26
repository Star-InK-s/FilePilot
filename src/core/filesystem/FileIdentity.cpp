#include "core/filesystem/FileIdentity.h"

#include <QDir>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <filesystem>
#include <functional>
#endif

namespace FilePilot {

#ifdef Q_OS_WIN

bool inspectFilesystemPath(
    const QString &path,
    FilesystemPathInfo &info,
    QString &error)
{
    info = FilesystemPathInfo{};

    const QString nativePath = QDir::toNativeSeparators(path);
    HANDLE handle = CreateFileW(
        reinterpret_cast<LPCWSTR>(nativePath.utf16()),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
            info.inspected = true;
            info.kind = FilesystemPathKind::Missing;
            return true;
        }

        error = QStringLiteral("无法读取文件系统路径身份");
        return false;
    }

    BY_HANDLE_FILE_INFORMATION handleInfo{};
    const BOOL queried = GetFileInformationByHandle(handle, &handleInfo);
    CloseHandle(handle);
    if (queried == FALSE) {
        error = QStringLiteral("无法读取文件系统对象身份");
        return false;
    }

    const bool directory =
        (handleInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    const bool reparse =
        (handleInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

    info.inspected = true;
    info.kind = reparse
        ? FilesystemPathKind::ReparsePoint
        : directory
            ? FilesystemPathKind::Directory
            : (handleInfo.dwFileAttributes & FILE_ATTRIBUTE_DEVICE) != 0
                ? FilesystemPathKind::Other
                : FilesystemPathKind::RegularFile;
    info.identity.valid = true;
    info.identity.volumeSerial = handleInfo.dwVolumeSerialNumber;
    info.identity.fileId =
        (static_cast<quint64>(handleInfo.nFileIndexHigh) << 32)
        | handleInfo.nFileIndexLow;
    info.size = static_cast<qint64>(
        (static_cast<quint64>(handleInfo.nFileSizeHigh) << 32)
        | handleInfo.nFileSizeLow);
    info.modifiedMSecs =
        (static_cast<qint64>(handleInfo.ftLastWriteTime.dwHighDateTime) << 32)
        | handleInfo.ftLastWriteTime.dwLowDateTime;
    return true;
}

#else

bool inspectFilesystemPath(
    const QString &path,
    FilesystemPathInfo &info,
    QString &error)
{
    info = FilesystemPathInfo{};
    std::error_code code;
    const std::filesystem::path nativePath =
        std::filesystem::u8path(path.toStdString());
    const std::filesystem::file_status status =
        std::filesystem::symlink_status(nativePath, code);
    if (code) {
        error = QStringLiteral("无法读取文件系统路径身份");
        return false;
    }

    info.inspected = true;
    if (std::filesystem::is_symlink(status)) {
        info.kind = FilesystemPathKind::ReparsePoint;
    } else if (std::filesystem::is_regular_file(status)) {
        info.kind = FilesystemPathKind::RegularFile;
    } else if (std::filesystem::is_directory(status)) {
        info.kind = FilesystemPathKind::Directory;
    } else if (std::filesystem::exists(status)) {
        info.kind = FilesystemPathKind::Other;
    } else {
        info.kind = FilesystemPathKind::Missing;
        return true;
    }

    std::error_code metadataError;
    const auto size = std::filesystem::file_size(nativePath, metadataError);
    if (!metadataError) {
        info.size = static_cast<qint64>(size);
    }
    const auto modified = std::filesystem::last_write_time(
        nativePath, metadataError);
    if (!metadataError) {
        info.modifiedMSecs = static_cast<qint64>(
            modified.time_since_epoch().count());
    }

    const std::string canonicalPath =
        std::filesystem::weakly_canonical(nativePath, metadataError).string();
    info.identity.valid = true;
    info.identity.fileId = std::hash<std::string>{}(canonicalPath)
        ^ (static_cast<quint64>(info.size) << 1)
        ^ static_cast<quint64>(info.modifiedMSecs);
    return true;
}

#endif

} // namespace FilePilot
