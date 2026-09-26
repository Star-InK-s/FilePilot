#pragma once

#include "core/duplicates/DuplicateTypes.h"
#include "core/scan/ScanService.h"

#include <functional>

namespace FilePilot {

using DuplicateProgressCallback =
    std::function<void(const DuplicateProgress &)>;
using DuplicateErrorCallback =
    std::function<void(const DuplicateError &)>;

class DuplicateFinder
{
public:
    DuplicateResult findDuplicates(
        const ScanResult &scanResult,
        const ScanCancellationToken &cancellationToken,
        const DuplicateProgressCallback &progressCallback = {},
        const DuplicateErrorCallback &errorCallback = {}) const;
};

} // namespace FilePilot
