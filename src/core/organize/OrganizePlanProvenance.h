#pragma once

#include <QString>

namespace FilePilot {

enum class TargetRootKind {
    Empty,
    Relative,
    Absolute,
    Unc,
    DeviceNamespace
};

QString targetRootKindName(TargetRootKind kind);

struct OrganizePlanProvenance {
    QString normalizedTargetRoot;
    TargetRootKind targetRootKind = TargetRootKind::Empty;
    quint64 planGeneration = 0;
    quint64 scanGeneration = 0;
    QString scanSourceRoot;
};

} // namespace FilePilot
