#pragma once

#include <QString>

#include <QtGlobal>

namespace FilePilot {

struct FileIdentity {
    bool valid = false;
    quint32 volumeSerial = 0;
    quint64 fileId = 0;

    friend bool operator==(const FileIdentity &left, const FileIdentity &right)
    {
        return left.valid == right.valid
            && left.volumeSerial == right.volumeSerial
            && left.fileId == right.fileId;
    }

    friend bool operator!=(const FileIdentity &left, const FileIdentity &right)
    {
        return !(left == right);
    }
};

enum class FilesystemPathKind {
    Missing,
    RegularFile,
    Directory,
    ReparsePoint,
    Other,
};

struct FilesystemPathInfo {
    bool inspected = false;
    FilesystemPathKind kind = FilesystemPathKind::Missing;
    FileIdentity identity;
    qint64 size = -1;
    qint64 modifiedMSecs = 0;

    bool exists() const
    {
        return kind != FilesystemPathKind::Missing;
    }

    bool isRegularFile() const
    {
        return kind == FilesystemPathKind::RegularFile;
    }

    bool isDirectory() const
    {
        return kind == FilesystemPathKind::Directory;
    }

    bool isReparsePoint() const
    {
        return kind == FilesystemPathKind::ReparsePoint;
    }
};

bool inspectFilesystemPath(
    const QString &path,
    FilesystemPathInfo &info,
    QString &error);

} // namespace FilePilot
