#pragma once

#include "core/execution/ExecutionTypes.h"

#include <QSet>

#include <vector>

namespace FilePilot {

class ConflictResolver
{
public:
    std::vector<ConflictDecision> resolve(
        const std::vector<OrganizePlanItem> &items,
        ConflictPolicy policy) const;

    ConflictDecision resolveSingle(
        const OrganizePlanItem &item,
        ConflictPolicy policy,
        const QSet<QString> &reservedDestinations) const;

private:
    QString nextAvailableDestination(
        const OrganizePlanItem &item,
        const QSet<QString> &reservedDestinations) const;
};

} // namespace FilePilot
