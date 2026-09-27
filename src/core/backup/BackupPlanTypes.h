#pragma once

#include "core/filesystem/FileIdentity.h"
#include "core/filesystem/FileSnapshot.h"
#include "core/model/ConflictPolicy.h"

#include <QDateTime>
#include <QString>

#include <vector>

namespace FilePilot {

enum class BackupEntryKind {
    File,
    Directory
};

enum class BackupPlanItemStatus {
    Ready,
    Invalid,
    Unsupported
};

struct BackupUnsupportedEntry {
    QString relativePath;
    QString sourcePath;
    FilesystemPathKind kind = FilesystemPathKind::Missing;
    QString reason;
};

struct BackupVolumeInfo {
    bool valid = false;
    quint32 serial = 0;
    QString rootPath;
};

struct BackupPlanItem {
    QString relativePath;
    QString sourcePath;
    QString plannedDestinationPath;
    BackupEntryKind kind = BackupEntryKind::File;
    FileIdentity expectedSourceIdentity;
    qint64 expectedSize = -1;
    QDateTime expectedModifiedTime;
    FileSnapshot sourceSnapshot;
    ConflictPolicy conflictPolicy = ConflictPolicy::AutoRename;
    FileIdentity expectedDestinationIdentity;
    BackupPlanItemStatus plannedStatus = BackupPlanItemStatus::Ready;
};

struct BackupPlan {
    QString operationId;
    QString sourcePath;
    BackupEntryKind sourceKind = BackupEntryKind::File;
    QString destinationRoot;
    QString finalDestinationPath;
    ConflictPolicy conflictPolicy = ConflictPolicy::AutoRename;
    FileSnapshot sourceRootSnapshot;
    FileSnapshot destinationRootSnapshot;
    BackupVolumeInfo sourceVolume;
    BackupVolumeInfo destinationVolume;
    std::vector<BackupPlanItem> items;
    qint64 expectedFileCount = 0;
    qint64 expectedDirectoryCount = 0;
    qint64 expectedBytes = 0;
    std::vector<BackupUnsupportedEntry> unsupportedEntries;
    quint64 planGeneration = 0;
};

struct BackupInventory {
    QString sourceRoot;
    FileSnapshot sourceRootSnapshot;
    std::vector<BackupPlanItem> items;
    std::vector<BackupUnsupportedEntry> unsupportedEntries;
    qint64 expectedFileCount = 0;
    qint64 expectedDirectoryCount = 0;
    qint64 expectedBytes = 0;
};

} // namespace FilePilot
