#include "ui/models/DuplicateModels.h"

#include <QDir>
#include <QLocale>

#include <algorithm>
#include <utility>

namespace FilePilot {

namespace {

QString duplicateHashStatusText(const DuplicateHashStatus status)
{
    switch (status) {
    case DuplicateHashStatus::Pending:
        return QStringLiteral("等待校验");
    case DuplicateHashStatus::PartialHashed:
        return QStringLiteral("部分校验");
    case DuplicateHashStatus::FullHashed:
        return QStringLiteral("完整校验");
    case DuplicateHashStatus::NotDuplicate:
        return QStringLiteral("非重复");
    case DuplicateHashStatus::Failed:
        return QStringLiteral("校验失败");
    case DuplicateHashStatus::Changed:
        return QStringLiteral("文件已变化");
    case DuplicateHashStatus::Cancelled:
        return QStringLiteral("已取消");
    }
    return {};
}

} // namespace

QString duplicateFileSizeText(const qint64 sizeBytes)
{
    return QLocale::system().formattedDataSize(sizeBytes);
}

DuplicateGroupModel::DuplicateGroupModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int DuplicateGroupModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(groups_.size());
}

int DuplicateGroupModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant DuplicateGroupModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()
        || index.row() < 0
        || index.row() >= static_cast<int>(groups_.size())) {
        return {};
    }

    const DuplicateGroup &group = groups_.at(static_cast<size_t>(index.row()));
    if (role == Qt::TextAlignmentRole
        && (index.column() == FileCount
            || index.column() == FileSize
            || index.column() == WastedSize)) {
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    }
    if (role != Qt::DisplayRole && role != Qt::ToolTipRole) {
        return {};
    }

    switch (index.column()) {
    case Group:
        return QStringLiteral("#%1").arg(group.groupId);
    case FileCount:
        return group.duplicateCount;
    case FileSize:
        return duplicateFileSizeText(group.fileSize);
    case WastedSize:
        return duplicateFileSizeText(group.wastedSize);
    case Summary: {
        QString summary;
        const size_t count = std::min<size_t>(group.items.size(), 2);
        for (size_t itemIndex = 0; itemIndex < count; ++itemIndex) {
            if (!summary.isEmpty()) {
                summary += QStringLiteral("  ·  ");
            }
            summary += QDir::toNativeSeparators(group.items.at(itemIndex).path);
        }
        if (group.items.size() > count) {
            summary += QStringLiteral("  ·  等 %1 个文件").arg(group.items.size());
        }
        return summary;
    }
    default:
        return {};
    }
}

QVariant DuplicateGroupModel::headerData(const int section,
                                         const Qt::Orientation orientation,
                                         const int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return {};
    }

    switch (section) {
    case Group:
        return QStringLiteral("组号");
    case FileCount:
        return QStringLiteral("重复项");
    case FileSize:
        return QStringLiteral("单文件大小");
    case WastedSize:
        return QStringLiteral("可回收");
    case Summary:
        return QStringLiteral("文件摘要");
    default:
        return {};
    }
}

void DuplicateGroupModel::setGroups(std::vector<DuplicateGroup> groups)
{
    beginResetModel();
    groups_ = std::move(groups);
    endResetModel();
}

void DuplicateGroupModel::clear()
{
    setGroups({});
}

const DuplicateGroup *DuplicateGroupModel::groupAt(const int row) const
{
    if (row < 0 || row >= static_cast<int>(groups_.size())) {
        return nullptr;
    }
    return &groups_.at(static_cast<size_t>(row));
}

DuplicateItemModel::DuplicateItemModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int DuplicateItemModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(items_.size());
}

int DuplicateItemModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant DuplicateItemModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()
        || index.row() < 0
        || index.row() >= static_cast<int>(items_.size())) {
        return {};
    }

    const DuplicateItem &item = items_.at(static_cast<size_t>(index.row()));
    if (role == Qt::TextAlignmentRole && index.column() == Size) {
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    }
    if (role != Qt::DisplayRole && role != Qt::ToolTipRole) {
        return {};
    }

    switch (index.column()) {
    case Path:
        return QDir::toNativeSeparators(item.path);
    case Size:
        return duplicateFileSizeText(item.size);
    case ModifiedTime:
        return QLocale::system().toString(
            item.modifiedUtc.toLocalTime(),
            QLocale::ShortFormat);
    case HashStatus:
        return duplicateHashStatusText(item.status);
    case ErrorMessage:
        return item.errorMessage;
    default:
        return {};
    }
}

QVariant DuplicateItemModel::headerData(const int section,
                                        const Qt::Orientation orientation,
                                        const int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return {};
    }

    switch (section) {
    case Path:
        return QStringLiteral("文件路径");
    case Size:
        return QStringLiteral("大小");
    case ModifiedTime:
        return QStringLiteral("修改时间");
    case HashStatus:
        return QStringLiteral("校验状态");
    case ErrorMessage:
        return QStringLiteral("错误信息");
    default:
        return {};
    }
}

void DuplicateItemModel::setItems(std::vector<DuplicateItem> items)
{
    beginResetModel();
    items_ = std::move(items);
    endResetModel();
}

void DuplicateItemModel::clear()
{
    setItems({});
}

DuplicateErrorModel::DuplicateErrorModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int DuplicateErrorModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(errors_.size());
}

int DuplicateErrorModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant DuplicateErrorModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()
        || index.row() < 0
        || index.row() >= static_cast<int>(errors_.size())) {
        return {};
    }

    const DuplicateError &error = errors_.at(static_cast<size_t>(index.row()));
    if (role == Qt::TextAlignmentRole && index.column() == Code) {
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    }
    if (role != Qt::DisplayRole && role != Qt::ToolTipRole) {
        return {};
    }

    switch (index.column()) {
    case Path:
        return QDir::toNativeSeparators(error.path);
    case Message:
        return error.message;
    case Code:
        return error.code;
    default:
        return {};
    }
}

QVariant DuplicateErrorModel::headerData(const int section,
                                         const Qt::Orientation orientation,
                                         const int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return {};
    }

    switch (section) {
    case Path:
        return QStringLiteral("文件路径");
    case Message:
        return QStringLiteral("错误信息");
    case Code:
        return QStringLiteral("错误代码");
    default:
        return {};
    }
}

void DuplicateErrorModel::setErrors(std::vector<DuplicateError> errors)
{
    beginResetModel();
    errors_ = std::move(errors);
    endResetModel();
}

void DuplicateErrorModel::clear()
{
    setErrors({});
}

} // namespace FilePilot
