#pragma once

#include "core/organize/OrganizePlanItem.h"
#include "core/organize/OrganizePlanProvenance.h"

#include <QHash>
#include <QString>

#include <vector>

namespace FilePilot {

class OrganizePlan
{
public:
    void add(OrganizePlanItem item);
    void clear();

    const std::vector<OrganizePlanItem> &items() const;
    std::vector<OrganizePlanItem> plannedItems() const;
    std::vector<OrganizePlanItem> executableCandidates() const;
    std::size_t count() const;
    bool isEmpty() const;

    qint64 plannedCount() const;
    qint64 invalidCount() const;
    qint64 noOpCount() const;
    QHash<QString, qint64> categoryCounts() const;

    const OrganizePlanProvenance &provenance() const;
    void setProvenance(OrganizePlanProvenance provenance);

    bool isCurrentFor(const QString &targetRoot,
                      quint64 planGeneration,
                      quint64 scanGeneration,
                      const QString &scanSourceRoot) const;

private:
    std::vector<OrganizePlanItem> items_;
    OrganizePlanProvenance provenance_;
};

} // namespace FilePilot
