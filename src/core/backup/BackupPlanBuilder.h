#pragma once

#include "core/backup/BackupPlanTypes.h"
#include "core/model/ConflictPolicy.h"

namespace FilePilot {

class BackupPlanBuilder
{
public:
    bool build(
        const QString &sourcePath,
        const QString &destinationRoot,
        ConflictPolicy conflictPolicy,
        BackupPlan &plan,
        QString &error) const;
};

} // namespace FilePilot
