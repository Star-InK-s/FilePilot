#include "ui/models/OrganizePreviewModel.h"

#include "core/organize/OrganizePlanItem.h"

namespace FilePilot {

OrganizePreviewModel::OrganizePreviewModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int OrganizePreviewModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(plan_.count());
}

int OrganizePreviewModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant OrganizePreviewModel::data(const QModelIndex &index, const int role) const
{
    if (!index.isValid()
        || index.row() < 0
        || index.row() >= rowCount()
        || index.column() < 0
        || index.column() >= ColumnCount) {
        return {};
    }

    const OrganizePlanItem &item =
        plan_.items().at(static_cast<std::size_t>(index.row()));

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case FileName:
            return item.fileName;
        case SourcePath:
            return item.sourcePath;
        case Category:
            return item.category.isEmpty() ? QStringLiteral("Invalid") : item.category;
        case DestinationPath:
            return item.planStatus == OrganizePlanStatus::Invalid || item.destinationPath.isEmpty()
                ? QStringLiteral("—")
                : item.destinationPath;
        case Status:
            return organizePlanStatusName(item.planStatus);
        default:
            return {};
        }
    }

    if (role == Qt::ToolTipRole) {
        return item.errorMessage.isEmpty()
            ? item.destinationPath
            : item.errorMessage;
    }

    return {};
}

QVariant OrganizePreviewModel::headerData(
    const int section,
    const Qt::Orientation orientation,
    const int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return {};
    }

    switch (section) {
    case FileName:
        return QStringLiteral("文件名");
    case SourcePath:
        return QStringLiteral("当前路径");
    case Category:
        return QStringLiteral("分类");
    case DestinationPath:
        return QStringLiteral("目标路径");
    case Status:
        return QStringLiteral("状态");
    default:
        return {};
    }
}

void OrganizePreviewModel::setPlan(OrganizePlan plan)
{
    beginResetModel();
    plan_ = std::move(plan);
    endResetModel();
}

void OrganizePreviewModel::clear()
{
    setPlan(OrganizePlan{});
}

const OrganizePlan &OrganizePreviewModel::plan() const
{
    return plan_;
}

} // namespace FilePilot
