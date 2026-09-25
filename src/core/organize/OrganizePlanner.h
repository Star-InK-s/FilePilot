#pragma once

#include "core/organize/OrganizePlan.h"
#include "core/scan/ScanService.h"

#include <QString>

namespace FilePilot {

class OrganizePlanner
{
public:
    explicit OrganizePlanner(QString targetRoot);

    OrganizePlan plan(const ScanResult &scanResult) const;

private:
    QString targetRoot_;
};

} // namespace FilePilot
