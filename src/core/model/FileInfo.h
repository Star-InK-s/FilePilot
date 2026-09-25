#pragma once

#include <QDateTime>
#include <QString>

namespace FilePilot {

enum class FileKind {
    RegularFile,
    Directory,
    SymbolicLink,
    Other
};

struct FileInfo {
    QString absolutePath;
    QString fileName;
    QString extension;
    qint64 sizeBytes = 0;
    QDateTime createdUtc;
    QDateTime modifiedUtc;
    FileKind kind = FileKind::RegularFile;
    QString category;

    bool isValid() const
    {
        return !absolutePath.isEmpty() && !fileName.isEmpty();
    }
};

inline bool operator==(const FileInfo &left, const FileInfo &right)
{
    return left.absolutePath == right.absolutePath
        && left.fileName == right.fileName
        && left.extension == right.extension
        && left.sizeBytes == right.sizeBytes
        && left.createdUtc == right.createdUtc
        && left.modifiedUtc == right.modifiedUtc
        && left.kind == right.kind
        && left.category == right.category;
}

inline bool operator!=(const FileInfo &left, const FileInfo &right)
{
    return !(left == right);
}

} // namespace FilePilot
