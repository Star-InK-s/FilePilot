#pragma once

#include "core/organize/OrganizePlan.h"

#include <QAbstractTableModel>

namespace FilePilot {

class OrganizePreviewModel : public QAbstractTableModel
{
public:
    enum Column {
        FileName = 0,
        SourcePath,
        Category,
        DestinationPath,
        Status,
        ColumnCount
    };

    explicit OrganizePreviewModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section,
                        Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void setPlan(OrganizePlan plan);
    void clear();
    const OrganizePlan &plan() const;

private:
    OrganizePlan plan_;
};

} // namespace FilePilot
