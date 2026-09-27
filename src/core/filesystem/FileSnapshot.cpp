#include "core/filesystem/FileSnapshot.h"

#include "core/filesystem/FileHasher.h"

#include <QFileInfo>

namespace FilePilot {

bool captureFileSnapshot(const QString &path,
                         FileSnapshot &snapshot,
                         AppError &error)
{
    snapshot = FileSnapshot{};
    snapshot.path = path;
    error = AppError{};

    FilesystemPathInfo info;
    QString inspectionError;
    if (!inspectFilesystemPath(path, info, inspectionError) || !info.inspected) {
        error = AppError{
            ErrorCode::Io,
            inspectionError.isEmpty()
                ? QStringLiteral("无法读取文件系统对象快照")
                : inspectionError,
            path,
        };
        return false;
    }

    snapshot.kind = info.kind;
    snapshot.identity = info.identity;
    snapshot.size = info.size;
    snapshot.reparsePoint = info.isReparsePoint();

    if (info.kind == FilesystemPathKind::Missing) {
        error = AppError{
            ErrorCode::NotFound,
            QStringLiteral("文件系统对象不存在"),
            path,
        };
        return false;
    }

    const QFileInfo fileInfo(path);
    snapshot.modifiedTime = fileInfo.lastModified();

    if (info.isRegularFile()) {
        FileHasher hasher;
        if (!hasher.hashFile(path, snapshot.sha256, error)) {
            snapshot = FileSnapshot{};
            snapshot.path = path;
            return false;
        }
    }

    snapshot.valid = info.identity.valid;
    if (!snapshot.valid) {
        error = AppError{
            ErrorCode::Io,
            QStringLiteral("文件系统对象身份不可验证"),
            path,
        };
        snapshot = FileSnapshot{};
        snapshot.path = path;
        return false;
    }

    return true;
}

} // namespace FilePilot
