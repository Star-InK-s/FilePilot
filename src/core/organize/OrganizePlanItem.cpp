#include "core/organize/OrganizePlanItem.h"

namespace FilePilot {

QString organizePlanStatusName(const OrganizePlanStatus status)
{
    switch (status) {
    case OrganizePlanStatus::Planned:
        return QStringLiteral("Planned");
    case OrganizePlanStatus::Invalid:
        return QStringLiteral("Invalid");
    }

    return QStringLiteral("Invalid");
}

} // namespace FilePilot
