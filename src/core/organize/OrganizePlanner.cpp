#include "core/organize/OrganizePlanner.h"

#include "core/organize/OrganizePathValidator.h"

#include <utility>

namespace FilePilot {

OrganizePlanner::OrganizePlanner(QString targetRoot)
    : targetRoot_(std::move(targetRoot))
{
}

OrganizePlan OrganizePlanner::plan(
    const ScanResult &scanResult,
    const quint64 planGeneration,
    const quint64 scanGeneration) const
{
    OrganizePlan plan;
    const TargetRootInfo rootInfo =
        OrganizePathValidator::inspectTargetRoot(targetRoot_);
    plan.setProvenance(OrganizePlanProvenance{
        rootInfo.normalizedPath,
        rootInfo.kind,
        planGeneration,
        scanGeneration,
        OrganizePathValidator::normalizePath(scanResult.rootPath),
    });

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

        if (!rootInfo.valid) {
            item.planStatus = OrganizePlanStatus::Invalid;
            item.errorMessage = rootInfo.message;
            plan.add(std::move(item));
            continue;
        }

        const QString destination = OrganizePathValidator::destinationPath(
            rootInfo.normalizedPath,
            file.category,
            file.fileName);
        if (!OrganizePathValidator::isPathInsideRoot(
                rootInfo.normalizedPath, destination)) {
            item.planStatus = OrganizePlanStatus::Invalid;
            item.errorMessage = QStringLiteral("目标路径超出目标根目录");
            plan.add(std::move(item));
            continue;
        }

        item.destinationPath = destination;
        if (OrganizePathValidator::pathsEqual(file.absolutePath, destination)) {
            item.planStatus = OrganizePlanStatus::NoOp;
        } else {
            item.planStatus = OrganizePlanStatus::Planned;
        }
        plan.add(std::move(item));
    }

    return plan;
}

} // namespace FilePilot
