#include "ui/models/FileTableModel.h"

#include <QLocale>

namespace FilePilot {

FileTableModel::FileTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int FileTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(files_.size());
}

int FileTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant FileTableModel::data(const QModelIndex &index, const int role) const
{
    if (!index.isValid()
        || index.row() < 0
        || index.row() >= rowCount()
        || index.column() < 0
        || index.column() >= ColumnCount) {
        return {};
    }

    const FileInfo &file = files_.at(static_cast<std::size_t>(index.row()));

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case FileName:
            return file.fileName;
        case Type:
            return typeLabel(file);
        case Size:
            return formatFileSize(file.sizeBytes);
        case ModifiedTime:
            return file.modifiedUtc.isValid()
                ? QLocale().toString(file.modifiedUtc.toLocalTime(),
                                     QLocale::ShortFormat)
                : QStringLiteral("—");
        case Path:
            return file.absolutePath;
        default:
            return {};
        }
    }

    if (role == SortRole) {
        switch (index.column()) {
        case FileName:
            return file.fileName.toCaseFolded();
        case Type:
            return typeLabel(file);
        case Size:
            return file.sizeBytes;
        case ModifiedTime:
            return file.modifiedUtc.toMSecsSinceEpoch();
        case Path:
            return file.absolutePath.toCaseFolded();
        default:
            return {};
        }
    }

    if (role == Qt::ToolTipRole) {
        return file.absolutePath;
    }

    if (role == Qt::TextAlignmentRole
        && index.column() == Size) {
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    }

    return {};
}

QVariant FileTableModel::headerData(const int section,
                                    const Qt::Orientation orientation,
                                    const int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return {};
    }

    switch (section) {
    case FileName:
        return QStringLiteral("文件名");
    case Type:
        return QStringLiteral("类型");
    case Size:
        return QStringLiteral("大小");
    case ModifiedTime:
        return QStringLiteral("修改时间");
    case Path:
        return QStringLiteral("路径");
    default:
        return {};
    }
}

void FileTableModel::setFiles(std::vector<FileInfo> files)
{
    beginResetModel();
    files_ = std::move(files);
    endResetModel();
}

void FileTableModel::clear()
{
    setFiles({});
}

const std::vector<FileInfo> &FileTableModel::files() const
{
    return files_;
}

QString FileTableModel::formatFileSize(const qint64 sizeBytes)
{
    if (sizeBytes < 0) {
        return QStringLiteral("—");
    }

    const QStringList units{
        QStringLiteral("B"),
        QStringLiteral("KB"),
        QStringLiteral("MB"),
        QStringLiteral("GB"),
        QStringLiteral("TB"),
    };

    double value = static_cast<double>(sizeBytes);
    int unitIndex = 0;
    while (value >= 1024.0 && unitIndex < units.size() - 1) {
        value /= 1024.0;
        ++unitIndex;
    }

    if (unitIndex == 0) {
        return QStringLiteral("%1 B").arg(sizeBytes);
    }

    return QStringLiteral("%1 %2")
        .arg(value, 0, 'f', value < 10.0 ? 1 : 0)
        .arg(units.at(unitIndex));
}

QString FileTableModel::typeLabel(const FileInfo &file)
{
    const QString category = file.category.trimmed();
    if (category.compare(QStringLiteral("Images"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("图片");
    }
    if (category.compare(QStringLiteral("Videos"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("视频");
    }
    if (category.compare(QStringLiteral("Documents"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("文档");
    }
    if (category.compare(QStringLiteral("Archives"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("压缩包");
    }
    return QStringLiteral("其他");
}

} // namespace FilePilot
