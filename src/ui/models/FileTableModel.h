#pragma once

#include "core/model/FileInfo.h"

#include <QAbstractTableModel>

#include <vector>

namespace FilePilot {

class FileTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        FileName = 0,
        Type,
        Size,
        ModifiedTime,
        Path,
        ColumnCount
    };

    explicit FileTableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section,
                        Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void setFiles(std::vector<FileInfo> files);
    void clear();

    const std::vector<FileInfo> &files() const;

    static QString formatFileSize(qint64 sizeBytes);

private:
    static QString typeLabel(const FileInfo &file);

    std::vector<FileInfo> files_;
};

} // namespace FilePilot
