#pragma once

#include "core/filesystem/FileIdentity.h"
#include "core/model/AppError.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>

namespace FilePilot {

struct FileSnapshot {
    QString path;
    FilesystemPathKind kind = FilesystemPathKind::Missing;
    FileIdentity identity;
    qint64 size = -1;
    QDateTime modifiedTime;
    QByteArray sha256;
    bool valid = false;
    bool reparsePoint = false;
};

bool captureFileSnapshot(const QString &path,
                         FileSnapshot &snapshot,
                         AppError &error);

} // namespace FilePilot
