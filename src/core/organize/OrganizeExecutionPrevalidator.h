#pragma once

#include "core/organize/OrganizePlanItem.h"
#include "core/organize/OrganizePlanProvenance.h"

#include <QString>

namespace FilePilot {

struct ExecutionValidationResult {
    bool valid = false;
    QString message;
};

// Phase 5 implements this boundary with real filesystem checks. OrganizePlan
// candidates are lexical planning results and are never execution-safe on their own.
class OrganizeExecutionPrevalidator
{
public:
    virtual ~OrganizeExecutionPrevalidator() = default;

    virtual ExecutionValidationResult validateTargetRoot(
        const OrganizePlanProvenance &provenance) const = 0;

    virtual ExecutionValidationResult validateCandidate(
        const OrganizePlanProvenance &provenance,
        const OrganizePlanItem &item) const = 0;
};

} // namespace FilePilot
