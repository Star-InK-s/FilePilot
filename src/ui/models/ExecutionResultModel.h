#pragma once

#include "core/execution/ExecutionTypes.h"

#include <QAbstractTableModel>

namespace FilePilot {

class ExecutionResultModel : public QAbstractTableModel
{
public:
    enum Column {
        SourcePath = 0,
        DestinationPath,
        Status,
        Message,
        ColumnCount
    };

    explicit ExecutionResultModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section,
                        Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void setResult(ExecutionResult result);
    void clear();
    const ExecutionResult &result() const;

    static QString statusText(ExecutionItemStatus status);
    static QString summaryText(const ExecutionSummary &summary);

private:
    ExecutionResult result_;
};

} // namespace FilePilot
