#include "core/organize/OrganizePlanner.h"

#include "core/organize/OrganizePathValidator.h"

namespace FilePilot {

OrganizePlanner::OrganizePlanner(QString targetRoot)
    : targetRoot_(std::move(targetRoot))
{
}

OrganizePlan OrganizePlanner::plan(const ScanResult &scanResult) const
{
    OrganizePlan plan;
    const QString normalizedRoot =
        OrganizePathValidator::normalizePath(targetRoot_);

    for (const FileInfo &file : scanResult.files) {
        OrganizePlanItem item;
        item.sourcePath = file.absolutePath;
        item.category = file.category;
        item.fileName = file.fileName;
        item.fileSize = file.sizeBytes;
        item.modifiedTime = file.modifiedUtc;

        const PathValidationResult categoryValidation =
            OrganizePathValidator::validateCategoryName(file.category);
        if (!categoryValidation.valid) {
            item.planStatus = OrganizePlanStatus::Invalid;
            item.errorMessage = categoryValidation.message;
            plan.add(std::move(item));
            continue;
        }

        const PathValidationResult fileValidation =
            OrganizePathValidator::validateFileName(file.fileName);
        if (!fileValidation.valid) {
            item.planStatus = OrganizePlanStatus::Invalid;
            item.errorMessage = fileValidation.message;
            plan.add(std::move(item));
            continue;
        }

        if (normalizedRoot.isEmpty()) {
            item.planStatus = OrganizePlanStatus::Invalid;
            item.errorMessage = QStringLiteral("目标根目录不能为空");
            plan.add(std::move(item));
            continue;
        }

        const QString destination = OrganizePathValidator::destinationPath(
            normalizedRoot,
            file.category,
            file.fileName);
        if (!OrganizePathValidator::isPathInsideRoot(normalizedRoot, destination)) {
            item.planStatus = OrganizePlanStatus::Invalid;
            item.errorMessage = QStringLiteral("目标路径超出目标根目录");
            plan.add(std::move(item));
            continue;
        }

        item.destinationPath = destination;
        item.planStatus = OrganizePlanStatus::Planned;
        plan.add(std::move(item));
    }

    return plan;
}

} // namespace FilePilot
