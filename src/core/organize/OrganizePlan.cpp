#include "core/organize/OrganizePlan.h"

#include "core/organize/OrganizePathValidator.h"

#include <utility>

namespace FilePilot {

void OrganizePlan::add(OrganizePlanItem item)
{
    items_.push_back(std::move(item));
}

void OrganizePlan::clear()
{
    items_.clear();
    provenance_ = OrganizePlanProvenance{};
}

const std::vector<OrganizePlanItem> &OrganizePlan::items() const
{
    return items_;
}

std::vector<OrganizePlanItem> OrganizePlan::plannedItems() const
{
    std::vector<OrganizePlanItem> result;
    for (const OrganizePlanItem &item : items_) {
        if (item.planStatus == OrganizePlanStatus::Planned) {
            result.push_back(item);
        }
    }
    return result;
}

std::vector<OrganizePlanItem> OrganizePlan::executableCandidates() const
{
    return plannedItems();
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

qint64 OrganizePlan::noOpCount() const
{
    qint64 count = 0;
    for (const OrganizePlanItem &item : items_) {
        if (item.planStatus == OrganizePlanStatus::NoOp) {
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

const OrganizePlanProvenance &OrganizePlan::provenance() const
{
    return provenance_;
}

void OrganizePlan::setProvenance(OrganizePlanProvenance provenance)
{
    provenance_ = std::move(provenance);
}

bool OrganizePlan::isCurrentFor(
    const QString &targetRoot,
    const quint64 planGeneration,
    const quint64 scanGeneration,
    const QString &scanSourceRoot) const
{
    return OrganizePathValidator::pathsEqual(provenance_.normalizedTargetRoot, targetRoot)
        && provenance_.planGeneration == planGeneration
        && provenance_.scanGeneration == scanGeneration
        && OrganizePathValidator::pathsEqual(
            provenance_.scanSourceRoot,
            scanSourceRoot);
}

} // namespace FilePilot
