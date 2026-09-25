#pragma once

#include "core/organize/OrganizePlan.h"
#include "core/scan/ScanService.h"

#include <QString>

namespace FilePilot {

class OrganizePlanner
{
public:
    explicit OrganizePlanner(QString targetRoot);

    OrganizePlan plan(const ScanResult &scanResult,
                      quint64 planGeneration = 1,
                      quint64 scanGeneration = 0) const;

private:
    QString targetRoot_;
};

} // namespace FilePilot
