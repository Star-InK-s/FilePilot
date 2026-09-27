#pragma once

#include "core/backup/BackupExecutionTypes.h"
#include "core/backup/BackupPlanTypes.h"

#include <atomic>

namespace FilePilot {

class BackupExecutor
{
public:
    BackupExecutionResult execute(
        const BackupPlan &plan,
        const std::atomic_bool &cancelled,
        const BackupExecutorHooks &hooks = {}) const;
};

} // namespace FilePilot
