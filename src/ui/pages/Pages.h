#pragma once

#include <QWidget>

class QString;
class QLabel;
class QPushButton;
class QTableView;

namespace FilePilot {

class ExecutionHistoryRepository;
class ExecutionHistoryModel;
class ExecutionResultModel;

class PlaceholderPage : public QWidget
{
public:
    explicit PlaceholderPage(const QString &title, QWidget *parent = nullptr);
};

class BackupPage : public PlaceholderPage
{
public:
    explicit BackupPage(QWidget *parent = nullptr);
};

class HistoryPage : public QWidget
{
    Q_OBJECT

public:
    explicit HistoryPage(
        ExecutionHistoryRepository &repository,
        QWidget *parent = nullptr);

private slots:
    void refresh();
    void showSelectedExecution();
    void deleteSelectedExecution();

private:
    ExecutionHistoryRepository &repository_;
    QTableView *historyTableView_ = nullptr;
    QTableView *detailTableView_ = nullptr;
    QLabel *historyStatusLabel_ = nullptr;
    ExecutionHistoryModel *historyModel_ = nullptr;
    ExecutionResultModel *detailModel_ = nullptr;
    QPushButton *deleteHistoryButton_ = nullptr;
};
class SettingsPage : public PlaceholderPage
{
public:
    explicit SettingsPage(QWidget *parent = nullptr);
};

} // namespace FilePilot
