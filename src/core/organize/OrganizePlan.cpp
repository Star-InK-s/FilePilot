#include "core/organize/OrganizePlan.h"

namespace FilePilot {

void OrganizePlan::add(OrganizePlanItem item)
{
    items_.push_back(std::move(item));
}

void OrganizePlan::clear()
{
    items_.clear();
}

const std::vector<OrganizePlanItem> &OrganizePlan::items() const
{
    return items_;
}

std::size_t OrganizePlan::count() const
{
    return items_.size();
}

bool OrganizePlan::isEmpty() const
{
    return items_.empty();
}

qint64 OrganizePlan::plannedCount() const
{
    qint64 count = 0;
    for (const OrganizePlanItem &item : items_) {
        if (item.planStatus == OrganizePlanStatus::Planned) {
            ++count;
        }
    }
    return count;
}

qint64 OrganizePlan::invalidCount() const
{
    qint64 count = 0;
    for (const OrganizePlanItem &item : items_) {
        if (item.planStatus == OrganizePlanStatus::Invalid) {
            ++count;
        }
    }
    return count;
}

QHash<QString, qint64> OrganizePlan::categoryCounts() const
{
    QHash<QString, qint64> counts;
    for (const OrganizePlanItem &item : items_) {
        const QString category = item.category.trimmed().isEmpty()
            ? QStringLiteral("Invalid")
            : item.category;
        ++counts[category];
    }
    return counts;
}

} // namespace FilePilot
