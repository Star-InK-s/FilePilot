#pragma once

#include "core/duplicates/DuplicateTypes.h"

#include <QAbstractTableModel>

#include <vector>

namespace FilePilot {

class DuplicateGroupModel : public QAbstractTableModel
{
public:
    enum Column {
        Group = 0,
        FileCount,
        FileSize,
        WastedSize,
        Summary,
        ColumnCount
    };

    explicit DuplicateGroupModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section,
                        Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void setGroups(std::vector<DuplicateGroup> groups);
    void clear();
    const DuplicateGroup *groupAt(int row) const;

private:
    std::vector<DuplicateGroup> groups_;
};

class DuplicateItemModel : public QAbstractTableModel
{
public:
    enum Column {
        Path = 0,
        Size,
        ModifiedTime,
        HashStatus,
        ErrorMessage,
        ColumnCount
    };

    explicit DuplicateItemModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section,
                        Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void setItems(std::vector<DuplicateItem> items);
    void clear();

private:
    std::vector<DuplicateItem> items_;
};

class DuplicateErrorModel : public QAbstractTableModel
{
public:
    enum Column {
        Path = 0,
        Message,
        Code,
        ColumnCount
    };

    explicit DuplicateErrorModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section,
                        Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void setErrors(std::vector<DuplicateError> errors);
    void clear();

private:
    std::vector<DuplicateError> errors_;
};

QString duplicateFileSizeText(qint64 sizeBytes);

} // namespace FilePilot
