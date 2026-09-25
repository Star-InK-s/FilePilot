#include "core/organize/OrganizePlanItem.h"

namespace FilePilot {

QString organizePlanStatusName(const OrganizePlanStatus status)
{
    switch (status) {
    case OrganizePlanStatus::Planned:
        return QStringLiteral("Planned");
    case OrganizePlanStatus::Invalid:
        return QStringLiteral("Invalid");
    case OrganizePlanStatus::NoOp:
        return QStringLiteral("NoOp");
    }

    return QStringLiteral("Invalid");
}

} // namespace FilePilot
