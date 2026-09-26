#include "ui/models/ExecutionHistoryModel.h"

namespace FilePilot {

namespace {

QString finalStateText(const QString &state)
{
    if (state == QStringLiteral("Idle")) {
        return QStringLiteral("尚未开始");
    }
    if (state == QStringLiteral("Preparing")) {
        return QStringLiteral("准备中");
    }
    if (state == QStringLiteral("Running")) {
        return QStringLiteral("执行中");
    }
    if (state == QStringLiteral("Cancelling")) {
        return QStringLiteral("正在取消");
    }
    if (state == QStringLiteral("Completed")) {
        return QStringLiteral("已完成");
    }
    if (state == QStringLiteral("Completed with errors")) {
        return QStringLiteral("部分完成");
    }
    if (state == QStringLiteral("Cancelled")) {
        return QStringLiteral("已取消");
    }
    if (state == QStringLiteral("Failed")) {
        return QStringLiteral("失败");
    }
    return state;
}

} // namespace


ExecutionHistoryModel::ExecutionHistoryModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int ExecutionHistoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(records_.size());
}

int ExecutionHistoryModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant ExecutionHistoryModel::data(const QModelIndex &index, const int role) const
{
    if (!index.isValid()
        || index.row() < 0
        || index.row() >= rowCount()
        || index.column() < 0
        || index.column() >= ColumnCount) {
        return {};
    }

    const ExecutionHistoryRecord &record =
        records_.at(static_cast<std::size_t>(index.row()));
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case CreatedAt:
            return record.createdAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        case SourceRoot:
            return record.sourceRoot;
        case TargetRoot:
            return record.targetRoot;
        case FinalState:
            return finalStateText(record.finalState);
        case Processed:
            return record.summary.planned;
        case Succeeded:
            return record.summary.succeeded;
        case Skipped:
            return record.summary.skipped;
        case Rejected:
            return record.summary.rejected;
        case Failed:
            return record.summary.failed;
        case CleanupFailed:
            return record.summary.sourceCleanupFailed;
        case Cancelled:
            return record.summary.cancelled;
        default:
            return {};
        }
    }

    if (role == Qt::ToolTipRole) {
        return record.fatalError.isEmpty()
            ? finalStateText(record.finalState)
            : record.fatalError;
    }

    return {};
}

QVariant ExecutionHistoryModel::headerData(
    const int section,
    const Qt::Orientation orientation,
    const int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return {};
    }

    switch (section) {
    case CreatedAt:
        return QStringLiteral("时间");
    case SourceRoot:
        return QStringLiteral("来源目录");
    case TargetRoot:
        return QStringLiteral("目标目录");
    case FinalState:
        return QStringLiteral("最终状态");
    case Processed:
        return QStringLiteral("处理");
    case Succeeded:
        return QStringLiteral("成功");
    case Skipped:
        return QStringLiteral("跳过");
    case Rejected:
        return QStringLiteral("拒绝");
    case Failed:
        return QStringLiteral("失败");
    case CleanupFailed:
        return QStringLiteral("清理失败");
    case Cancelled:
        return QStringLiteral("取消");
    default:
        return {};
    }
}

void ExecutionHistoryModel::setRecords(std::vector<ExecutionHistoryRecord> records)
{
    beginResetModel();
    records_ = std::move(records);
    endResetModel();
}

void ExecutionHistoryModel::clear()
{
    setRecords({});
}

const ExecutionHistoryRecord *ExecutionHistoryModel::recordAt(const int row) const
{
    if (row < 0 || row >= rowCount()) {
        return nullptr;
    }
    return &records_.at(static_cast<std::size_t>(row));
}

const std::vector<ExecutionHistoryRecord> &ExecutionHistoryModel::records() const
{
    return records_;
}

} // namespace FilePilot
