#include "core/organize/OrganizePlanProvenance.h"

namespace FilePilot {

QString targetRootKindName(const TargetRootKind kind)
{
    switch (kind) {
    case TargetRootKind::Empty:
        return QStringLiteral("Empty");
    case TargetRootKind::Relative:
        return QStringLiteral("Relative");
    case TargetRootKind::Absolute:
        return QStringLiteral("Absolute");
    case TargetRootKind::Unc:
        return QStringLiteral("UNC");
    case TargetRootKind::DeviceNamespace:
        return QStringLiteral("DeviceNamespace");
    }

    return QStringLiteral("Unknown");
}

} // namespace FilePilot
