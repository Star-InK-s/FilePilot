#pragma once

#include "core/backup/BackupPlanTypes.h"
#include "core/filesystem/FileSnapshot.h"
#include "core/model/AppError.h"

#include <QString>

namespace FilePilot {

enum class BackupValidationCode {
    Valid,
    MissingSource,
    DestinationNotCreatable,
    InvalidPath,
    UnsupportedReparse,
    PathRelationshipInvalid,
    VolumeUnavailable,
    SnapshotFailed
};

struct BackupValidationResult {
    bool valid = false;
    BackupValidationCode code = BackupValidationCode::SnapshotFailed;
    AppError error;
    FileSnapshot sourceRootSnapshot;
    FileSnapshot destinationRootSnapshot;
    BackupVolumeInfo sourceVolume;
    BackupVolumeInfo destinationVolume;
};

class BackupPrevalidator
{
public:
    BackupValidationResult validateRoots(const QString &source,
                                         const QString &destination) const;
};

} // namespace FilePilot
