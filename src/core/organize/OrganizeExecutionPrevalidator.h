#pragma once

#include "core/organize/OrganizePlanItem.h"
#include "core/organize/OrganizePlanProvenance.h"

#include <QString>

namespace FilePilot {

struct ExecutionValidationResult {
    bool valid = false;
    QString message;
};

class OrganizeExecutionPrevalidator
{
public:
    ExecutionValidationResult validateTargetRoot(
        const OrganizePlanProvenance &provenance) const;

    ExecutionValidationResult validateCandidate(
        const OrganizePlanProvenance &provenance,
        const OrganizePlanItem &item) const;

private:
    struct TargetRootSnapshot {
        bool valid = false;
        quint32 volumeSerial = 0;
        quint64 fileId = 0;
    };

    mutable bool targetRootCaptured_ = false;
    mutable TargetRootSnapshot targetRootSnapshot_;
};

} // namespace FilePilot
