#pragma once

#include "core/backup/BackupPlanTypes.h"
#include "core/model/AppError.h"

#include <QString>

namespace FilePilot {

class BackupInventoryBuilder
{
public:
    bool build(const QString &sourceRoot,
               BackupInventory &inventory,
               AppError &error) const;
};

} // namespace FilePilot
