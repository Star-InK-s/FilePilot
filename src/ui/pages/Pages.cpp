#include "ui/pages/Pages.h"

#include "core/database/ExecutionHistoryRepository.h"
#include "ui/models/ExecutionHistoryModel.h"
#include "ui/models/ExecutionResultModel.h"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPushButton>
#include <QTableView>
#include <QVBoxLayout>
#include <optional>

namespace FilePilot {

PlaceholderPage::PlaceholderPage(const QString &title, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 28, 32, 28);
    layout->setSpacing(18);

    auto *titleLabel = new QLabel(title, this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(16);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    layout->addWidget(titleLabel);

    auto *emptyLabel = new QLabel(QStringLiteral("暂无数据"), this);
    emptyLabel->setObjectName(QStringLiteral("emptyStateLabel"));
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLabel->setEnabled(false);
    layout->addWidget(emptyLabel, 1);
}

HistoryPage::HistoryPage(
    ExecutionHistoryRepository &repository,
    QWidget *parent)
    : QWidget(parent)
    , repository_(repository)
{
    setObjectName(QStringLiteral("pageHistory"));
    setWindowTitle(QStringLiteral("历史记录"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 28, 32, 28);
    layout->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("执行历史"), this);
    QFont titleFont = title->font();
    titleFont.setPointSize(16);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto *toolbar = new QHBoxLayout();
    toolbar->addStretch(1);
    auto *refreshButton = new QPushButton(QStringLiteral("刷新"), this);
    refreshButton->setObjectName(QStringLiteral("refreshHistoryButton"));
    toolbar->addWidget(refreshButton);
    deleteHistoryButton_ = new QPushButton(QStringLiteral("删除选中记录"), this);
    deleteHistoryButton_->setObjectName(QStringLiteral("deleteHistoryButton"));
    deleteHistoryButton_->setEnabled(false);
    toolbar->addWidget(deleteHistoryButton_);
    layout->addLayout(toolbar);

    historyTableView_ = new QTableView(this);
    historyTableView_->setObjectName(QStringLiteral("historyTableView"));
    historyModel_ = new ExecutionHistoryModel(historyTableView_);
    historyTableView_->setModel(historyModel_);
    historyTableView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    historyTableView_->setSelectionMode(QAbstractItemView::SingleSelection);
    historyTableView_->setAlternatingRowColors(true);
    historyTableView_->verticalHeader()->setVisible(false);
    historyTableView_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(historyTableView_, 2);

    auto *detailTitle = new QLabel(QStringLiteral("执行详情"), this);
    detailTitle->setObjectName(QStringLiteral("historyDetailTitle"));
    layout->addWidget(detailTitle);

    detailTableView_ = new QTableView(this);
    detailTableView_->setObjectName(QStringLiteral("historyDetailTableView"));
    detailModel_ = new ExecutionResultModel(detailTableView_);
    detailTableView_->setModel(detailModel_);
    detailTableView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    detailTableView_->setAlternatingRowColors(true);
    detailTableView_->verticalHeader()->setVisible(false);
    detailTableView_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(detailTableView_, 2);

    historyStatusLabel_ = new QLabel(QStringLiteral("尚未加载历史记录"), this);
    historyStatusLabel_->setObjectName(QStringLiteral("historyStatusLabel"));
    historyStatusLabel_->setWordWrap(true);
    layout->addWidget(historyStatusLabel_);

    connect(refreshButton, &QPushButton::clicked,
            this, &HistoryPage::refresh);
    connect(deleteHistoryButton_, &QPushButton::clicked,
            this, &HistoryPage::deleteSelectedExecution);
    connect(
        historyTableView_->selectionModel(),
        &QItemSelectionModel::selectionChanged,
        this,
        &HistoryPage::showSelectedExecution);

    refresh();
}

void HistoryPage::refresh()
{
    QString error;
    const std::vector<ExecutionHistoryRecord> records =
        repository_.listExecutions(&error);
    if (!error.isEmpty()) {
        historyModel_->clear();
        detailModel_->clear();
        historyStatusLabel_->setText(
            QStringLiteral("历史记录加载失败：%1").arg(error));
        deleteHistoryButton_->setEnabled(false);
        return;
    }

    historyModel_->setRecords(records);
    detailModel_->clear();
    historyStatusLabel_->setText(
        QStringLiteral("共 %1 条执行历史").arg(records.size()));
    deleteHistoryButton_->setEnabled(!records.empty());
}

void HistoryPage::showSelectedExecution()
{
    const QModelIndexList selected =
        historyTableView_->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        detailModel_->clear();
        deleteHistoryButton_->setEnabled(false);
        return;
    }

    const ExecutionHistoryRecord *record =
        historyModel_->recordAt(selected.first().row());
    if (record == nullptr) {
        detailModel_->clear();
        return;
    }

    QString error;
    const std::optional<ExecutionHistoryDetail> detail =
        repository_.getExecution(record->id, &error);
    if (!detail.has_value()) {
        detailModel_->clear();
        historyStatusLabel_->setText(
            QStringLiteral("执行详情加载失败：%1").arg(error));
        return;
    }

    detailModel_->setResult(detail->result);
    historyStatusLabel_->setText(
        QStringLiteral("已加载执行详情，共 %1 个文件项目")
            .arg(detail->result.items.size()));
    deleteHistoryButton_->setEnabled(true);
}

void HistoryPage::deleteSelectedExecution()
{
    const QModelIndexList selected =
        historyTableView_->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        return;
    }

    const ExecutionHistoryRecord *record =
        historyModel_->recordAt(selected.first().row());
    if (record == nullptr) {
        return;
    }

    QString error;
    if (!repository_.deleteExecution(record->id, &error)) {
        historyStatusLabel_->setText(
            QStringLiteral("历史记录删除失败：%1").arg(error));
        return;
    }

    refresh();
}
SettingsPage::SettingsPage(QWidget *parent)
    : PlaceholderPage(QStringLiteral("设置"), parent)
{
    setObjectName(QStringLiteral("pageSettings"));
}

} // namespace FilePilot
