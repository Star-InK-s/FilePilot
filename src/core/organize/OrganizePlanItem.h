#pragma once

#include <QDateTime>
#include <QString>

namespace FilePilot {

enum class OrganizePlanStatus {
    Planned,
    Invalid,
    NoOp
};

QString organizePlanStatusName(OrganizePlanStatus status);

struct OrganizePlanItem {
    QString sourcePath;
    QString destinationPath;
    QString category;
    QString fileName;
    qint64 fileSize = 0;
    QDateTime modifiedTime;
    OrganizePlanStatus planStatus = OrganizePlanStatus::Planned;
    QString errorMessage;
};

} // namespace FilePilot
