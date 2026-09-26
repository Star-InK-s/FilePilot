#include "core/duplicates/DuplicateTypes.h"

namespace FilePilot {

QString duplicateHashStatusName(const DuplicateHashStatus status)
{
    switch (status) {
    case DuplicateHashStatus::Pending:
        return QStringLiteral("Pending");
    case DuplicateHashStatus::PartialHashed:
        return QStringLiteral("PartialHashed");
    case DuplicateHashStatus::FullHashed:
        return QStringLiteral("FullHashed");
    case DuplicateHashStatus::NotDuplicate:
        return QStringLiteral("NotDuplicate");
    case DuplicateHashStatus::Failed:
        return QStringLiteral("Failed");
    case DuplicateHashStatus::Changed:
        return QStringLiteral("Changed");
    case DuplicateHashStatus::Cancelled:
        return QStringLiteral("Cancelled");
    }

    return QStringLiteral("Failed");
}

QString duplicatePhaseName(const DuplicatePhase phase)
{
    switch (phase) {
    case DuplicatePhase::CandidateFiltering:
        return QStringLiteral("CandidateFiltering");
    case DuplicatePhase::PartialHashing:
        return QStringLiteral("PartialHashing");
    case DuplicatePhase::FullHashing:
        return QStringLiteral("FullHashing");
    case DuplicatePhase::Completed:
        return QStringLiteral("Completed");
    case DuplicatePhase::Cancelled:
        return QStringLiteral("Cancelled");
    case DuplicatePhase::Failed:
        return QStringLiteral("Failed");
    }

    return QStringLiteral("Failed");
}

} // namespace FilePilot
