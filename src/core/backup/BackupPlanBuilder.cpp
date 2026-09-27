#include "core/backup/BackupPlanBuilder.h"

#include "core/backup/BackupInventoryBuilder.h"

#include <QDir>
#include <QFileInfo>
#include <QUuid>

namespace FilePilot {

bool BackupPlanBuilder::build(
    const QString &sourcePath,
    const QString &destinationRoot,
    const ConflictPolicy conflictPolicy,
    BackupPlan &plan,
    QString &error) const
{
    if (sourcePath.trimmed().isEmpty() || destinationRoot.trimmed().isEmpty()) {
        error = QStringLiteral("请选择备份源和备份目标根目录");
        return false;
    }

    BackupInventory inventory;
    AppError inventoryError;
    if (!BackupInventoryBuilder().build(sourcePath, inventory, inventoryError)) {
        error = inventoryError.message().isEmpty()
            ? QStringLiteral("无法生成备份预览")
            : inventoryError.message();
        return false;
    }

    plan = BackupPlan{};
    plan.operationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    plan.sourcePath = sourcePath;
    plan.sourceKind = inventory.sourceRootSnapshot.kind == FilesystemPathKind::Directory
        ? BackupEntryKind::Directory
        : BackupEntryKind::File;
    plan.destinationRoot = destinationRoot;
    plan.finalDestinationPath =
        QDir(destinationRoot).filePath(QFileInfo(sourcePath).fileName());
    plan.conflictPolicy = conflictPolicy;
    plan.sourceRootSnapshot = inventory.sourceRootSnapshot;
    plan.items = inventory.items;
    plan.unsupportedEntries = inventory.unsupportedEntries;
    plan.expectedFileCount = inventory.expectedFileCount;
    plan.expectedDirectoryCount = inventory.expectedDirectoryCount;
    plan.expectedBytes = inventory.expectedBytes;
    plan.planGeneration = 1;

    for (BackupPlanItem &item : plan.items) {
        item.plannedDestinationPath =
            plan.sourceKind == BackupEntryKind::Directory
            ? QDir(plan.finalDestinationPath).filePath(item.relativePath)
            : plan.finalDestinationPath;
        item.conflictPolicy = conflictPolicy;
    }
    return true;
}

} // namespace FilePilot
