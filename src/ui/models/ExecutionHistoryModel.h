#pragma once

#include "core/database/HistoryTypes.h"

#include <QAbstractTableModel>

#include <vector>

namespace FilePilot {

class ExecutionHistoryModel : public QAbstractTableModel
{
public:
    enum Column {
        CreatedAt = 0,
        SourceRoot,
        TargetRoot,
        FinalState,
        Processed,
        Succeeded,
        Skipped,
        Rejected,
        Failed,
        CleanupFailed,
        Cancelled,
        ColumnCount
    };

    explicit ExecutionHistoryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section,
                        Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void setRecords(std::vector<ExecutionHistoryRecord> records);
    void clear();
    const ExecutionHistoryRecord *recordAt(int row) const;
    const std::vector<ExecutionHistoryRecord> &records() const;

private:
    std::vector<ExecutionHistoryRecord> records_;
};

} // namespace FilePilot
