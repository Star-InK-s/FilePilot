#include "ui/models/ExecutionResultModel.h"

namespace FilePilot {

ExecutionResultModel::ExecutionResultModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int ExecutionResultModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(result_.items.size());
}

int ExecutionResultModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant ExecutionResultModel::data(const QModelIndex &index, const int role) const
{
    if (!index.isValid()
        || index.row() < 0
        || index.row() >= rowCount()
        || index.column() < 0
        || index.column() >= ColumnCount) {
        return {};
    }

    const ExecutionItemResult &item =
        result_.items.at(static_cast<std::size_t>(index.row()));
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case SourcePath:
            return item.item.sourcePath;
        case DestinationPath:
            return item.actualDestination.isEmpty()
                ? item.item.destinationPath
                : item.actualDestination;
        case Status:
            return statusText(item.status);
        case Message:
            return item.errorMessage.isEmpty()
                ? QStringLiteral("—")
                : item.errorMessage;
        default:
            return {};
        }
    }

    if (role == Qt::ToolTipRole) {
        return item.errorMessage.isEmpty()
            ? statusText(item.status)
            : item.errorMessage;
    }

    return {};
}

QVariant ExecutionResultModel::headerData(
    const int section,
    const Qt::Orientation orientation,
    const int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return {};
    }

    switch (section) {
    case SourcePath:
        return QStringLiteral("来源");
    case DestinationPath:
        return QStringLiteral("目标");
    case Status:
        return QStringLiteral("状态");
    case Message:
        return QStringLiteral("说明");
    default:
        return {};
    }
}

void ExecutionResultModel::setResult(ExecutionResult result)
{
    beginResetModel();
    result_ = std::move(result);
    endResetModel();
}

void ExecutionResultModel::clear()
{
    setResult(ExecutionResult{});
}

const ExecutionResult &ExecutionResultModel::result() const
{
    return result_;
}

QString ExecutionResultModel::statusText(const ExecutionItemStatus status)
{
    switch (status) {
    case ExecutionItemStatus::Succeeded:
        return QStringLiteral("成功");
    case ExecutionItemStatus::Skipped:
        return QStringLiteral("跳过");
    case ExecutionItemStatus::Failed:
        return QStringLiteral("失败");
    case ExecutionItemStatus::Rejected:
        return QStringLiteral("拒绝");
    case ExecutionItemStatus::Cancelled:
        return QStringLiteral("已取消");
    case ExecutionItemStatus::SourceCleanupFailed:
        return QStringLiteral("清理失败");
    }

    return QStringLiteral("失败");
}

QString ExecutionResultModel::summaryText(const ExecutionSummary &summary)
{
    return QStringLiteral(
        "处理：%1  成功：%2  跳过：%3  拒绝：%4  失败：%5  清理失败：%6  取消：%7")
        .arg(summary.planned)
        .arg(summary.succeeded)
        .arg(summary.skipped)
        .arg(summary.rejected)
        .arg(summary.failed)
        .arg(summary.sourceCleanupFailed)
        .arg(summary.cancelled);
}

} // namespace FilePilot
