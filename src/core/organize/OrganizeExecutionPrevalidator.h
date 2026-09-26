#pragma once

#include "core/filesystem/FileIdentity.h"
#include "core/organize/OrganizePlanItem.h"
#include "core/organize/OrganizePlanProvenance.h"

#include <QString>

#include <utility>

namespace FilePilot {

struct ExecutionValidationResult {
    ExecutionValidationResult() = default;

    ExecutionValidationResult(
        const bool isValid,
        QString validationMessage = {},
        FileIdentity identity = {})
        : valid(isValid)
        , message(std::move(validationMessage))
        , sourceIdentity(identity)
    {
    }

    bool valid = false;
    QString message;
    FileIdentity sourceIdentity;
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
