#pragma once

#include "core/organize/OrganizePlanItem.h"

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
    std::size_t count() const;
    bool isEmpty() const;

    qint64 plannedCount() const;
    qint64 invalidCount() const;
    QHash<QString, qint64> categoryCounts() const;

private:
    std::vector<OrganizePlanItem> items_;
};

} // namespace FilePilot
