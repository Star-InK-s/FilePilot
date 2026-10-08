#include "ui/pages/FileOrganizePage.h"

#include "app/Application.h"
#include "core/filesystem/BasicFileOperations.h"
#include "core/organize/OrganizePathValidator.h"
#include "core/execution/ExecutionTypes.h"
#include "core/organize/OrganizePlanner.h"
#include "ui/models/FileTableModel.h"
#include "ui/models/ExecutionResultModel.h"
#include "ui/models/OrganizePreviewModel.h"

#include <QDir>
#include <QFileDialog>
#include <QFrame>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QStyle>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>

namespace FilePilot {

FileOrganizePage::FileOrganizePage(Application &application, QWidget *parent)
    : QWidget(parent)
    , application_(application)
{
    buildUi();

    connect(&scanTask_, &ScanTask::stateChanged,
            this, &FileOrganizePage::handleTaskState);
    connect(&scanTask_, &ScanTask::progressChanged,
            this, &FileOrganizePage::handleProgress);
    connect(&scanTask_, &ScanTask::errorBatchReported,
            this, &FileOrganizePage::handleErrorBatch);
    connect(&scanTask_, &ScanTask::completed,
            this, &FileOrganizePage::handleCompleted);
    connect(&scanTask_, &ScanTask::failed,
            this, &FileOrganizePage::handleFailed);
    connect(&scanTask_, &ScanTask::cancelled,
            this, &FileOrganizePage::handleCancelled);

    const QString lastDirectory = application_.settings().lastDirectory();
    if (!lastDirectory.isEmpty()) {
        directoryEdit_->setText(lastDirectory);
    }
}

void FileOrganizePage::buildUi()
{
    setObjectName(QStringLiteral("pageOrganize"));

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(32, 28, 32, 24);
    rootLayout->setSpacing(14);

    auto *titleBlock = new QVBoxLayout();
    titleBlock->setSpacing(2);

    auto *titleLabel = new QLabel(QStringLiteral("文件管理"), this);
    titleLabel->setObjectName(QStringLiteral("pageTitleLabel"));
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(16);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleBlock->addWidget(titleLabel);

    auto *subtitleLabel = new QLabel(
        QStringLiteral("扫描、查看并整理本地文件"), this);
    subtitleLabel->setObjectName(QStringLiteral("pageSubtitleLabel"));
    titleBlock->addWidget(subtitleLabel);
    rootLayout->addLayout(titleBlock);

    auto *directoryRow = new QHBoxLayout();
    directoryRow->setSpacing(10);

    auto *directoryLabel = new QLabel(QStringLiteral("扫描目录"), this);
    directoryLabel->setMinimumWidth(58);
    directoryRow->addWidget(directoryLabel);

    directoryEdit_ = new QLineEdit(this);
    directoryEdit_->setObjectName(QStringLiteral("directoryEdit"));
    directoryEdit_->setPlaceholderText(QStringLiteral("选择要扫描的本地目录"));
    directoryEdit_->setClearButtonEnabled(true);
    directoryRow->addWidget(directoryEdit_, 1);

    chooseDirectoryButton_ = new QPushButton(
        style()->standardIcon(QStyle::SP_DirOpenIcon),
        QStringLiteral("浏览"),
        this);
    chooseDirectoryButton_->setObjectName(QStringLiteral("chooseDirectoryButton"));
    chooseDirectoryButton_->setMinimumWidth(82);
    directoryRow->addWidget(chooseDirectoryButton_);

    scanButton_ = new QPushButton(
        style()->standardIcon(QStyle::SP_BrowserReload),
        QStringLiteral("开始扫描"),
        this);
    scanButton_->setObjectName(QStringLiteral("scanButton"));
    scanButton_->setMinimumWidth(96);
    directoryRow->addWidget(scanButton_);
    rootLayout->addLayout(directoryRow);

    auto *summaryRow = new QHBoxLayout();
    summaryRow->setSpacing(22);

    fileCountValueLabel_ = new QLabel(QStringLiteral("文件：0"), this);
    fileCountValueLabel_->setObjectName(QStringLiteral("fileCountValueLabel"));
    summaryRow->addWidget(fileCountValueLabel_);

    totalSizeValueLabel_ = new QLabel(QStringLiteral("总大小：0 B"), this);
    totalSizeValueLabel_->setObjectName(QStringLiteral("totalSizeValueLabel"));
    summaryRow->addWidget(totalSizeValueLabel_);

    errorCountValueLabel_ = new QLabel(QStringLiteral("错误：0"), this);
    errorCountValueLabel_->setObjectName(QStringLiteral("errorCountValueLabel"));
    summaryRow->addWidget(errorCountValueLabel_);

    extensionStatsLabel_ = new QLabel(QStringLiteral("类型统计：暂无"), this);
    extensionStatsLabel_->setObjectName(QStringLiteral("extensionStatsLabel"));
    extensionStatsLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    extensionStatsLabel_->hide();

    categoryStatsLabel_ = new QLabel(QStringLiteral("分类统计：暂无"), this);
    categoryStatsLabel_->setObjectName(QStringLiteral("categoryStatsLabel"));
    categoryStatsLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    categoryStatsLabel_->setMinimumWidth(220);
    summaryRow->addWidget(categoryStatsLabel_, 1);
    rootLayout->addLayout(summaryRow);

    scanStatusLabel_ = new QLabel(
        QStringLiteral("请选择一个文件夹开始扫描"), this);
    scanStatusLabel_->setObjectName(QStringLiteral("scanStatusLabel"));
    scanStatusLabel_->setWordWrap(true);
    rootLayout->addWidget(scanStatusLabel_);

    auto *listToolbarRow = new QHBoxLayout();
    listToolbarRow->setSpacing(10);
    listToolbarRow->addWidget(new QLabel(QStringLiteral("搜索文件名"), this));
    searchEdit_ = new QLineEdit(this);
    searchEdit_->setObjectName(QStringLiteral("searchEdit"));
    searchEdit_->setPlaceholderText(QStringLiteral("按文件名搜索"));
    searchEdit_->setClearButtonEnabled(true);
    searchEdit_->setMaximumWidth(360);
    listToolbarRow->addWidget(searchEdit_);
    listToolbarRow->addStretch(1);

    copyFilesButton_ = new QPushButton(QStringLiteral("复制"), this);
    copyFilesButton_->setObjectName(QStringLiteral("copyFilesButton"));
    copyFilesButton_->setEnabled(false);
    copyFilesButton_->setMinimumWidth(76);
    listToolbarRow->addWidget(copyFilesButton_);
    moveFilesButton_ = new QPushButton(QStringLiteral("移动"), this);
    moveFilesButton_->setObjectName(QStringLiteral("moveFilesButton"));
    moveFilesButton_->setEnabled(false);
    moveFilesButton_->setMinimumWidth(76);
    listToolbarRow->addWidget(moveFilesButton_);
    deleteFilesButton_ = new QPushButton(QStringLiteral("删除"), this);
    deleteFilesButton_->setObjectName(QStringLiteral("deleteFilesButton"));
    deleteFilesButton_->setEnabled(false);
    deleteFilesButton_->setMinimumWidth(76);
    listToolbarRow->addWidget(deleteFilesButton_);
    rootLayout->addLayout(listToolbarRow);

    fileTableView_ = new QTableView(this);
    fileTableView_->setObjectName(QStringLiteral("fileTableView"));
    fileModel_ = new FileTableModel(fileTableView_);
    // The proxy keeps filtering and sorting out of the source model. The view
    // sees a searchable, sortable list while FileTableModel stores the files.
    fileProxyModel_ = new QSortFilterProxyModel(fileTableView_);
    fileProxyModel_->setSourceModel(fileModel_);
    fileProxyModel_->setFilterKeyColumn(FileTableModel::FileName);
    fileProxyModel_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    fileProxyModel_->setSortRole(FileTableModel::SortRole);
    fileTableView_->setModel(fileProxyModel_);
    fileTableView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    fileTableView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    fileTableView_->setAlternatingRowColors(true);
    fileTableView_->setSortingEnabled(true);
    fileTableView_->sortByColumn(FileTableModel::FileName, Qt::AscendingOrder);
    fileTableView_->setWordWrap(false);
    fileTableView_->setTextElideMode(Qt::ElideMiddle);
    fileTableView_->verticalHeader()->setVisible(false);
    fileTableView_->verticalHeader()->setDefaultSectionSize(34);
    fileTableView_->horizontalHeader()->setMinimumSectionSize(80);
    fileTableView_->horizontalHeader()->setStretchLastSection(true);
    fileTableView_->setColumnWidth(FileTableModel::FileName, 240);
    fileTableView_->setColumnWidth(FileTableModel::Type, 110);
    fileTableView_->setColumnWidth(FileTableModel::Size, 100);
    fileTableView_->setColumnWidth(FileTableModel::ModifiedTime, 150);
    rootLayout->addWidget(fileTableView_, 2);

    auto *previewPanel = new QFrame(this);
    previewPanel->setObjectName(QStringLiteral("organizePreviewPanel"));
    previewPanel->setFrameShape(QFrame::StyledPanel);
    auto *previewLayout = new QVBoxLayout(previewPanel);
    previewLayout->setContentsMargins(16, 12, 16, 12);
    previewLayout->setSpacing(10);

    auto *previewTitle = new QLabel(QStringLiteral("整理预览"), previewPanel);
    QFont previewTitleFont = previewTitle->font();
    previewTitleFont.setBold(true);
    previewTitle->setFont(previewTitleFont);
    previewLayout->addWidget(previewTitle);

    auto *targetRow = new QHBoxLayout();
    targetRow->setSpacing(10);
    targetRow->addWidget(new QLabel(QStringLiteral("目标根目录"), previewPanel));
    targetRootEdit_ = new QLineEdit(previewPanel);
    targetRootEdit_->setObjectName(QStringLiteral("targetRootEdit"));
    targetRootEdit_->setPlaceholderText(QStringLiteral("选择整理目标根目录"));
    targetRootEdit_->setClearButtonEnabled(true);
    targetRow->addWidget(targetRootEdit_, 1);

    chooseTargetRootButton_ = new QPushButton(
        style()->standardIcon(QStyle::SP_DirOpenIcon),
        QStringLiteral("浏览"),
        previewPanel);
    chooseTargetRootButton_->setObjectName(QStringLiteral("chooseTargetRootButton"));
    targetRow->addWidget(chooseTargetRootButton_);

    generatePreviewButton_ = new QPushButton(
        style()->standardIcon(QStyle::SP_FileDialogDetailedView),
        QStringLiteral("生成整理预览"),
        previewPanel);
    generatePreviewButton_->setObjectName(QStringLiteral("generatePreviewButton"));
    generatePreviewButton_->setEnabled(false);
    targetRow->addWidget(generatePreviewButton_);
    previewLayout->addLayout(targetRow);

    auto *previewSummaryRow = new QHBoxLayout();
    previewSummaryRow->setSpacing(18);
    previewTotalLabel_ = new QLabel(QStringLiteral("总计划：0"), previewPanel);
    previewTotalLabel_->setObjectName(QStringLiteral("previewTotalLabel"));
    previewSummaryRow->addWidget(previewTotalLabel_);
    previewPlannedLabel_ = new QLabel(QStringLiteral("Planned：0"), previewPanel);
    previewPlannedLabel_->setObjectName(QStringLiteral("previewPlannedLabel"));
    previewSummaryRow->addWidget(previewPlannedLabel_);
    previewInvalidLabel_ = new QLabel(QStringLiteral("Invalid：0"), previewPanel);
        previewInvalidLabel_->setObjectName(QStringLiteral("previewInvalidLabel"));
    previewSummaryRow->addWidget(previewInvalidLabel_);

    previewNoOpLabel_ = new QLabel(QStringLiteral("NoOp：0"), previewPanel);
    previewNoOpLabel_->setObjectName(QStringLiteral("previewNoOpLabel"));
    previewSummaryRow->addWidget(previewNoOpLabel_);
    previewCategoryStatsLabel_ = new QLabel(QStringLiteral("分类统计：暂无"), previewPanel);
    previewCategoryStatsLabel_->setObjectName(QStringLiteral("previewCategoryStatsLabel"));
    previewCategoryStatsLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    previewSummaryRow->addWidget(previewCategoryStatsLabel_, 1);
    previewLayout->addLayout(previewSummaryRow);

    previewStatusLabel_ = new QLabel(QStringLiteral("尚未生成整理计划"), previewPanel);
    previewStatusLabel_->setObjectName(QStringLiteral("previewStatusLabel"));
    previewStatusLabel_->setWordWrap(true);
    previewLayout->addWidget(previewStatusLabel_);

    previewTableView_ = new QTableView(previewPanel);
    previewTableView_->setObjectName(QStringLiteral("previewTableView"));
    previewModel_ = new OrganizePreviewModel(previewTableView_);
    previewTableView_->setModel(previewModel_);
    previewTableView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    previewTableView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    previewTableView_->setAlternatingRowColors(true);
    previewTableView_->verticalHeader()->setVisible(false);
    previewTableView_->horizontalHeader()->setStretchLastSection(true);
    previewTableView_->setColumnWidth(OrganizePreviewModel::FileName, 170);
    previewTableView_->setColumnWidth(OrganizePreviewModel::SourcePath, 240);
    previewTableView_->setColumnWidth(OrganizePreviewModel::Category, 110);
    previewTableView_->setColumnWidth(OrganizePreviewModel::DestinationPath, 260);
    previewLayout->addWidget(previewTableView_, 1);

    auto *previewButtons = new QHBoxLayout();
    previewButtons->addStretch(1);
    confirmPlanButton_ = new QPushButton(
        style()->standardIcon(QStyle::SP_DialogApplyButton),
        QStringLiteral("确认计划"),
        previewPanel);
    confirmPlanButton_->setObjectName(QStringLiteral("confirmPlanButton"));
    confirmPlanButton_->setEnabled(false);
    previewButtons->addWidget(confirmPlanButton_);

    cancelPlanButton_ = new QPushButton(
        style()->standardIcon(QStyle::SP_DialogCancelButton),
        QStringLiteral("取消计划"),
        previewPanel);
    cancelPlanButton_->setObjectName(QStringLiteral("cancelPlanButton"));
    cancelPlanButton_->setEnabled(false);
    previewButtons->addWidget(cancelPlanButton_);

    executePlanButton_ = new QPushButton(
        style()->standardIcon(QStyle::SP_MediaPlay),
        QStringLiteral("开始执行"),
        previewPanel);
    executePlanButton_->setObjectName(QStringLiteral("executePlanButton"));
    executePlanButton_->setEnabled(false);
    previewButtons->addWidget(executePlanButton_);

    cancelExecutionButton_ = new QPushButton(
        style()->standardIcon(QStyle::SP_MediaStop),
        QStringLiteral("取消执行"),
        previewPanel);
    cancelExecutionButton_->setObjectName(QStringLiteral("cancelExecutionButton"));
    cancelExecutionButton_->setEnabled(false);
    previewButtons->addWidget(cancelExecutionButton_);

    previewLayout->addLayout(previewButtons);

    auto *executionRow = new QHBoxLayout();
    executionRow->setSpacing(10);
    executionCurrentFileLabel_ = new QLabel(QStringLiteral("当前文件：无"), previewPanel);
    executionCurrentFileLabel_->setObjectName(QStringLiteral("executionCurrentFileLabel"));
    executionCurrentFileLabel_->setWordWrap(true);
    executionRow->addWidget(executionCurrentFileLabel_, 1);

    executionSummaryLabel_ = new QLabel(QStringLiteral("成功：0  跳过：0  失败：0"), previewPanel);
    executionSummaryLabel_->setObjectName(QStringLiteral("executionSummaryLabel"));
    executionSummaryLabel_->setWordWrap(true);
    executionRow->addWidget(executionSummaryLabel_);
    previewLayout->addLayout(executionRow);

    executionProgressBar_ = new QProgressBar(previewPanel);
    executionProgressBar_->setObjectName(QStringLiteral("executionProgressBar"));
    executionProgressBar_->setRange(0, 100);
    executionProgressBar_->setValue(0);
    previewLayout->addWidget(executionProgressBar_);

    auto *executionResultTitle = new QLabel(QStringLiteral("执行结果"), previewPanel);
    QFont executionResultTitleFont = executionResultTitle->font();
    executionResultTitleFont.setBold(true);
    executionResultTitle->setFont(executionResultTitleFont);
    previewLayout->addWidget(executionResultTitle);

    executionResultTableView_ = new QTableView(previewPanel);
    executionResultTableView_->setObjectName(QStringLiteral("executionResultTableView"));
    executionResultModel_ = new ExecutionResultModel(executionResultTableView_);
    executionResultTableView_->setModel(executionResultModel_);
    executionResultTableView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    executionResultTableView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    executionResultTableView_->setAlternatingRowColors(true);
    executionResultTableView_->verticalHeader()->setVisible(false);
    executionResultTableView_->horizontalHeader()->setStretchLastSection(true);
    executionResultTableView_->setColumnWidth(ExecutionResultModel::SourcePath, 240);
    executionResultTableView_->setColumnWidth(ExecutionResultModel::DestinationPath, 260);
    executionResultTableView_->setColumnWidth(ExecutionResultModel::Status, 100);
    previewLayout->addWidget(executionResultTableView_, 1);
    rootLayout->addWidget(previewPanel, 2);

    // Keep the advanced organize flow compiled and tested, but leave it out of
    // the simplified v1 scan-and-list workflow.
    previewPanel->hide();

    connect(chooseDirectoryButton_, &QPushButton::clicked,
            this, &FileOrganizePage::chooseDirectory);
    connect(scanButton_, &QPushButton::clicked,
            this, &FileOrganizePage::startScan);
    connect(chooseTargetRootButton_, &QPushButton::clicked,
            this, &FileOrganizePage::chooseTargetRoot);
    connect(generatePreviewButton_, &QPushButton::clicked,
            this, &FileOrganizePage::generatePreview);
    connect(confirmPlanButton_, &QPushButton::clicked,
            this, &FileOrganizePage::confirmPlan);
    connect(cancelPlanButton_, &QPushButton::clicked,
            this, &FileOrganizePage::cancelPlan);
    connect(executePlanButton_, &QPushButton::clicked,
            this, &FileOrganizePage::startExecution);
    connect(cancelExecutionButton_, &QPushButton::clicked,
            this, &FileOrganizePage::cancelExecution);
    connect(&executionTask_, &OrganizeExecutionTask::stateChanged,
            this, &FileOrganizePage::handleExecutionState);
    connect(&executionTask_, &OrganizeExecutionTask::progressChanged,
            this, &FileOrganizePage::handleExecutionProgress);
    connect(&executionTask_, &OrganizeExecutionTask::itemProgressChanged,
            this, &FileOrganizePage::handleExecutionItemProgress);
    connect(&executionTask_, &OrganizeExecutionTask::completed,
            this, &FileOrganizePage::handleExecutionResult);
    connect(&executionTask_, &OrganizeExecutionTask::failed,
            this, &FileOrganizePage::handleExecutionFailure);
    connect(&executionTask_, &OrganizeExecutionTask::cancelled,
            this, &FileOrganizePage::handleExecutionCancelled);
    connect(targetRootEdit_, &QLineEdit::textChanged, this, [this](const QString &) {
        invalidatePlan(QStringLiteral("目标根目录已修改，当前整理计划已失效"));
    });
    connect(targetRootEdit_, &QLineEdit::returnPressed,
            this, &FileOrganizePage::generatePreview);
    connect(directoryEdit_, &QLineEdit::returnPressed,
            this, &FileOrganizePage::startScan);
    connect(searchEdit_, &QLineEdit::textChanged,
            fileProxyModel_, &QSortFilterProxyModel::setFilterFixedString);
    connect(copyFilesButton_, &QPushButton::clicked,
            this, &FileOrganizePage::copySelectedFiles);
    connect(moveFilesButton_, &QPushButton::clicked,
            this, &FileOrganizePage::moveSelectedFiles);
    connect(deleteFilesButton_, &QPushButton::clicked,
            this, &FileOrganizePage::deleteSelectedFiles);
    connect(fileTableView_->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this,
            [this](const QItemSelection &, const QItemSelection &) {
                const bool hasSelection =
                    fileTableView_->selectionModel()->hasSelection();
                copyFilesButton_->setEnabled(hasSelection);
                moveFilesButton_->setEnabled(hasSelection);
                deleteFilesButton_->setEnabled(hasSelection);
            });
}

void FileOrganizePage::chooseTargetRoot()
{
    const QString initialDirectory = targetRootEdit_->text().isEmpty()
        ? currentRoot_
        : targetRootEdit_->text();

    const QString directory = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("选择整理目标根目录"),
        initialDirectory);

    if (!directory.isEmpty()) {
        targetRootEdit_->setText(directory);
    }
}

void FileOrganizePage::generatePreview()
{
    if (!hasScanResult_) {
        previewStatusLabel_->setText(QStringLiteral("请先完成文件扫描"));
        return;
    }

    if (targetRootEdit_->text().trimmed().isEmpty()) {
        chooseTargetRoot();
    }

    const QString targetRoot = targetRootEdit_->text().trimmed();
    if (targetRoot.isEmpty()) {
        previewStatusLabel_->setText(QStringLiteral("请选择整理目标根目录"));
        return;
    }

    ++planGeneration_;
    currentPlan_ = OrganizePlanner(targetRoot).plan(
        lastScanResult_, planGeneration_, scanGeneration_);
    planConfirmed_ = false;
    previewModel_->setPlan(currentPlan_);
    updatePreviewSummary();

    const bool hasItems = !currentPlan_.isEmpty();
    const bool canConfirm =
        hasItems && currentPlan_.invalidCount() == 0;
    confirmPlanButton_->setEnabled(canConfirm);
    cancelPlanButton_->setEnabled(hasItems);

    if (!hasItems) {
        previewStatusLabel_->setText(QStringLiteral("没有可整理的文件"));
    } else if (!canConfirm) {
        previewStatusLabel_->setText(
            QStringLiteral("预览包含 %1 个无效计划项，无法确认")
                .arg(currentPlan_.invalidCount()));
    } else {
        previewStatusLabel_->setText(QStringLiteral("整理预览已生成"));
    }
}

void FileOrganizePage::confirmPlan()
{
    if (currentPlan_.isEmpty() || planConfirmed_) {
        return;
    }

    if (currentPlan_.invalidCount() > 0) {
        previewStatusLabel_->setText(
            QStringLiteral("计划包含无效项，无法确认"));
        return;
    }

    planConfirmed_ = true;
    confirmPlanButton_->setEnabled(false);
    cancelPlanButton_->setEnabled(true);
    previewStatusLabel_->setText(
        QStringLiteral("整理计划已确认，实际文件操作将在后续版本执行。"));
}

void FileOrganizePage::cancelPlan()
{
    clearPreview();
    previewStatusLabel_->setText(QStringLiteral("整理计划已取消"));
}

void FileOrganizePage::invalidatePlan(const QString &reason)
{
    const bool hadPlan = !currentPlan_.isEmpty() || planConfirmed_;
    currentPlan_.clear();
    planConfirmed_ = false;
    if (previewModel_ != nullptr) {
        previewModel_->clear();
    }
    if (previewTotalLabel_ != nullptr) {
        updatePreviewSummary();
    }
    if (confirmPlanButton_ != nullptr) {
        confirmPlanButton_->setEnabled(false);
    }
    if (cancelPlanButton_ != nullptr) {
        cancelPlanButton_->setEnabled(false);
    }
    if (generatePreviewButton_ != nullptr) {
        generatePreviewButton_->setEnabled(hasScanResult_);
    }
    if (previewStatusLabel_ != nullptr && (hadPlan || !reason.isEmpty())) {
        previewStatusLabel_->setText(reason);
    }
}

bool FileOrganizePage::planMatchesCurrentInputs() const
{
    return hasScanResult_
        && !currentPlan_.isEmpty()
        && currentPlan_.isCurrentFor(
            targetRootEdit_->text().trimmed(),
            planGeneration_,
            scanGeneration_,
            currentScanSourceRoot_);
}
void FileOrganizePage::resetExecutionPresentation()
{
    if (executionResultModel_ != nullptr) {
        executionResultModel_->clear();
    }
    if (executionCurrentFileLabel_ != nullptr) {
        executionCurrentFileLabel_->setText(QStringLiteral("当前项：无"));
    }
    if (executionSummaryLabel_ != nullptr) {
        executionSummaryLabel_->setText(QStringLiteral("尚未执行"));
    }
    if (executionProgressBar_ != nullptr) {
        executionProgressBar_->setRange(0, 0);
        executionProgressBar_->setValue(0);
    }
}
void FileOrganizePage::clearPreview()
{
    currentPlan_.clear();
    planConfirmed_ = false;
    if (previewModel_ != nullptr) {
        previewModel_->clear();
    }
    if (previewTotalLabel_ != nullptr) {
        updatePreviewSummary();
    }
    if (confirmPlanButton_ != nullptr) {
        confirmPlanButton_->setEnabled(false);
    }
    if (cancelPlanButton_ != nullptr) {
        cancelPlanButton_->setEnabled(false);
    }
    if (generatePreviewButton_ != nullptr) {
        generatePreviewButton_->setEnabled(hasScanResult_ && !planLocked_);
    }
    if (executePlanButton_ != nullptr) {
        executePlanButton_->setEnabled(false);
    }
    if (cancelExecutionButton_ != nullptr) {
        cancelExecutionButton_->setEnabled(false);
    }
    resetExecutionPresentation();
}
void FileOrganizePage::updatePreviewSummary()
{
    previewTotalLabel_->setText(
        QStringLiteral("总计划：%1").arg(currentPlan_.count()));
    previewPlannedLabel_->setText(
        QStringLiteral("Planned：%1").arg(currentPlan_.plannedCount()));
    previewInvalidLabel_->setText(
        QStringLiteral("Invalid：%1").arg(currentPlan_.invalidCount()));
    previewNoOpLabel_->setText(
        QStringLiteral("NoOp：%1").arg(currentPlan_.noOpCount()));
    previewCategoryStatsLabel_->setText(
        QStringLiteral("分类统计：%1")
            .arg(countSummary(currentPlan_.categoryCounts())));
}
void FileOrganizePage::chooseDirectory()
{
    const QString initialDirectory = directoryEdit_->text().isEmpty()
        ? application_.settings().lastDirectory()
        : directoryEdit_->text();

    const QString directory = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("选择扫描目录"),
        initialDirectory);

    if (directory.isEmpty()) {
        return;
    }

    directoryEdit_->setText(directory);
    application_.settings().setLastDirectory(directory);
    application_.settings().sync();
}

void FileOrganizePage::startScan()
{
    const QString rootPath = directoryEdit_->text().trimmed();
    if (rootPath.isEmpty()) {
        scanStatusLabel_->setText(QStringLiteral("请先选择一个文件夹"));
        return;
    }

    if (scanTask_.isActive()) {
        return;
    }

    const bool hadPlan = !currentPlan_.isEmpty() || planConfirmed_;
    currentRoot_ = rootPath;
    errorCount_ = 0;
    ++scanGeneration_;
    currentScanSourceRoot_.clear();
    hasScanResult_ = false;
    lastScanResult_ = ScanResult{};
    if (hadPlan) {
        invalidatePlan(QStringLiteral("开始新的扫描，旧整理计划已失效"));
    } else {
        clearPreview();
    }
    fileModel_->clear();
    updateSummary(0, 0, 0, {}, {});
    scanStatusLabel_->setText(QStringLiteral("正在准备扫描..."));
    scanButton_->setEnabled(false);
    chooseDirectoryButton_->setEnabled(false);
    directoryEdit_->setEnabled(false);

    application_.settings().setLastDirectory(rootPath);
    application_.settings().sync();

    if (!scanTask_.start(rootPath)) {
        scanStatusLabel_->setText(QStringLiteral("扫描任务当前不可用"));
        updateControls(TaskState::Idle);
    }
}

void FileOrganizePage::cancelScan()
{
    if (!scanTask_.isActive()) {
        return;
    }

    scanStatusLabel_->setText(QStringLiteral("正在取消扫描"));
    scanTask_.cancel();
}

QStringList FileOrganizePage::selectedFilePaths() const
{
    QStringList paths;
    if (fileTableView_ == nullptr || fileTableView_->selectionModel() == nullptr) {
        return paths;
    }

    const QModelIndexList selectedRows =
        fileTableView_->selectionModel()->selectedRows(FileTableModel::FileName);
    for (const QModelIndex &proxyIndex : selectedRows) {
        const QModelIndex sourceIndex = fileProxyModel_->mapToSource(proxyIndex);
        if (!sourceIndex.isValid() || sourceIndex.row() < 0
            || sourceIndex.row() >= fileModel_->rowCount()) {
            continue;
        }
        paths.push_back(
            fileModel_->files().at(static_cast<std::size_t>(sourceIndex.row()))
                .absolutePath);
    }
    return paths;
}

void FileOrganizePage::copySelectedFiles()
{
    runTargetOperation(false);
}

void FileOrganizePage::moveSelectedFiles()
{
    runTargetOperation(true);
}

void FileOrganizePage::runTargetOperation(const bool moveFiles)
{
    const QStringList sourcePaths = selectedFilePaths();
    const QString actionName =
        moveFiles ? QStringLiteral("移动") : QStringLiteral("复制");
    if (sourcePaths.isEmpty()) {
        QMessageBox::information(
            this,
            QStringLiteral("提示"),
            QStringLiteral("请先选择要%1的文件。").arg(actionName));
        return;
    }

    const QString targetDirectory = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("选择%1目标文件夹").arg(actionName),
        currentRoot_);
    if (targetDirectory.isEmpty()) {
        return;
    }

    QSet<QString> destinations;
    QStringList conflicts;
    for (const QString &sourcePath : sourcePaths) {
        const QString destination =
            QDir(targetDirectory).filePath(QFileInfo(sourcePath).fileName());
        const QString key = QDir::cleanPath(destination).toCaseFolded();
        if (QFileInfo::exists(destination) || destinations.contains(key)) {
            conflicts.push_back(destination);
        }
        destinations.insert(key);
    }
    if (!conflicts.isEmpty()) {
        QMessageBox::warning(
            this,
            QStringLiteral("目标文件已存在"),
            QStringLiteral("目标文件已存在，已取消当前操作。\n%1")
                .arg(conflicts.join(QStringLiteral("\\n"))));
        return;
    }

    QStringList errors;
    int succeeded = 0;
    for (const QString &sourcePath : sourcePaths) {
        QString errorMessage;
        const bool success = moveFiles
            ? FilePilot::BasicFileOperations::move(
                  sourcePath, targetDirectory, errorMessage)
            : FilePilot::BasicFileOperations::copy(
                  sourcePath, targetDirectory, errorMessage);
        if (success) {
            ++succeeded;
        } else {
            errors.push_back(errorMessage);
        }
    }

    if (errors.isEmpty()) {
        QMessageBox::information(
            this,
            QStringLiteral("%1完成").arg(actionName),
            QStringLiteral("%1完成：%2 个文件。").arg(actionName).arg(succeeded));
    } else {
        QMessageBox::warning(
            this,
            QStringLiteral("%1部分完成").arg(actionName),
            QStringLiteral("成功：%1 个，失败：%2 个。\n%3")
                .arg(succeeded)
                .arg(errors.size())
                .arg(errors.join(QStringLiteral("\n"))));
    }

    if (moveFiles && succeeded > 0) {
        startScan();
    }
}

void FileOrganizePage::deleteSelectedFiles()
{
    const QStringList sourcePaths = selectedFilePaths();
    if (sourcePaths.isEmpty()) {
        QMessageBox::information(
            this,
            QStringLiteral("提示"),
            QStringLiteral("请先选择要删除的文件。"));
        return;
    }

    const QMessageBox::StandardButton answer = QMessageBox::question(
        this,
        QStringLiteral("删除文件"),
        QStringLiteral("确定要删除选中的 %1 个文件吗？").arg(sourcePaths.size()),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }

    QStringList errors;
    int succeeded = 0;
    for (const QString &sourcePath : sourcePaths) {
        QString errorMessage;
        if (FilePilot::BasicFileOperations::remove(sourcePath, errorMessage)) {
            ++succeeded;
        } else {
            errors.push_back(errorMessage);
        }
    }

    if (errors.isEmpty()) {
        QMessageBox::information(
            this,
            QStringLiteral("删除完成"),
            QStringLiteral("删除完成：%1 个文件。").arg(succeeded));
    } else {
        QMessageBox::warning(
            this,
            QStringLiteral("删除部分完成"),
            QStringLiteral("成功：%1 个，失败：%2 个。\n%3")
                .arg(succeeded)
                .arg(errors.size())
                .arg(errors.join(QStringLiteral("\n"))));
    }

    if (succeeded > 0) {
        startScan();
    }
}

void FileOrganizePage::handleTaskState(const TaskState state)
{
    updateControls(state);
    emit taskStateChanged(state);

    switch (state) {
    case TaskState::Preparing:
        scanStatusLabel_->setText(QStringLiteral("正在准备扫描..."));
        break;
    case TaskState::Running:
        scanStatusLabel_->setText(QStringLiteral("正在扫描..."));
        break;
    case TaskState::Cancelling:
        scanStatusLabel_->setText(QStringLiteral("正在取消扫描"));
        break;
    case TaskState::Completed:
        scanStatusLabel_->setText(QStringLiteral("扫描完成"));
        break;
    case TaskState::CompletedWithErrors:
        scanStatusLabel_->setText(QStringLiteral("扫描完成，但有部分文件无法读取"));
        break;
    case TaskState::Cancelled:
        scanStatusLabel_->setText(QStringLiteral("扫描已取消"));
        break;
    case TaskState::Failed:
        break;
    case TaskState::Idle:
        break;
    }
}

void FileOrganizePage::handleProgress(const qint64 scannedFileCount,
                                      const QString currentDirectory, const QString currentFile)
{
    Q_UNUSED(currentFile);
    scanStatusLabel_->setText(
        QStringLiteral("正在扫描... 已扫描 %1 个文件").arg(scannedFileCount));
    emit taskProgressChanged(scannedFileCount, currentDirectory, currentFile);
}

void FileOrganizePage::handleErrorBatch(const ScanErrorBatch batch)
{
    errorCount_ = std::max(errorCount_, batch.totalErrorCount);
    errorCountValueLabel_->setText(QStringLiteral("错误：%1").arg(errorCount_));

    QStringList details;
    for (const ScanError &error : batch.errors) {
        details << QStringLiteral("%1：%2").arg(error.path, error.message);
    }
    if (!details.isEmpty()) {
        scanStatusLabel_->setToolTip(details.join(QStringLiteral("\n")));
    }

    emit taskErrorCountChanged(errorCount_);
}
void FileOrganizePage::handleCompleted(const ScanResult result)
{
    const bool hadPlan = !currentPlan_.isEmpty() || planConfirmed_;
    if (hadPlan) {
        invalidatePlan(QStringLiteral("新的扫描结果已生成，旧整理计划已失效"));
    }
    errorCount_ = result.statistics.errorCount;
    lastScanResult_ = result;
    currentScanSourceRoot_ = result.rootPath;
    hasScanResult_ = true;
    generatePreviewButton_->setEnabled(true);
    fileModel_->setFiles(result.files);

    QHash<QString, qint64> categoryCounts;
    for (const FileInfo &file : result.files) {
        ++categoryCounts[FileTableModel::typeLabel(file)];
    }

    updateSummary(
        result.statistics.fileCount,
        result.statistics.totalSizeBytes,
        result.statistics.errorCount,
        result.statistics.extensionCounts,
        categoryCounts);

    QStringList errorDetails;
    for (const ScanError &error : result.errors) {
        errorDetails << QStringLiteral("%1：%2").arg(error.path, error.message);
        if (errorDetails.size() >= 5) {
            break;
        }
    }
    scanStatusLabel_->setToolTip(errorDetails.join(QStringLiteral("\n")));

    if (result.statistics.errorCount > 0) {
        scanStatusLabel_->setText(
            QStringLiteral("扫描完成，发现 %1 个错误")
                .arg(result.statistics.errorCount));
    } else if (result.files.empty()) {
        scanStatusLabel_->setText(QStringLiteral("该文件夹中没有找到文件"));
    } else {
        scanStatusLabel_->setText(
            QStringLiteral("扫描完成：%1 个文件").arg(result.statistics.fileCount));
    }
    emit taskProgressChanged(
        result.statistics.fileCount, result.rootPath, QString());
}

void FileOrganizePage::handleFailed(const QString message)
{
    invalidatePlan(QStringLiteral("扫描失败，当前整理计划已失效"));
    fileModel_->clear();
    hasScanResult_ = false;
    lastScanResult_ = ScanResult{};
    clearPreview();
    updateSummary(0, 0, errorCount_, {}, {});
    scanStatusLabel_->setText(QStringLiteral("扫描失败：%1").arg(message));
}

void FileOrganizePage::handleCancelled()
{
    invalidatePlan(QStringLiteral("扫描已取消，当前整理计划已失效"));
    hasScanResult_ = false;
    lastScanResult_ = ScanResult{};
    clearPreview();
    scanStatusLabel_->setText(QStringLiteral("扫描已取消"));
}

void FileOrganizePage::startExecution()
{
    if (planLocked_ || !planConfirmed_ || !planMatchesCurrentInputs()) {
        return;
    }

    planLocked_ = true;
    executionState_ = TaskState::Preparing;
    resetExecutionPresentation();
    const qint64 total = currentPlan_.plannedCount();
    executionProgressBar_->setRange(0, static_cast<int>(std::max<qint64>(total, 0)));
    executionProgressBar_->setValue(0);
    ExecutionSummary initialSummary;
    initialSummary.planned = total;
    executionSummaryLabel_->setText(
        ExecutionResultModel::summaryText(initialSummary));
    updateControls(scanTask_.state());

    executionContext_ = ExecutionContext{
        targetRootEdit_->text().trimmed(),
        currentScanSourceRoot_,
        planGeneration_,
        scanGeneration_,
    };
    if (!executionTask_.start(
            currentPlan_, executionContext_, ConflictPolicy::AutoRename)) {
        planLocked_ = false;
        executionState_ = TaskState::Failed;
        previewStatusLabel_->setText(QStringLiteral("整理执行任务当前不可用"));
        updateControls(scanTask_.state());
    }
}

void FileOrganizePage::cancelExecution()
{
    if (!planLocked_
        || (executionState_ != TaskState::Preparing
            && executionState_ != TaskState::Running)) {
        return;
    }

    executionTask_.cancel();
    executionState_ = TaskState::Cancelling;
    previewStatusLabel_->setText(
        QStringLiteral("正在取消整理执行，请等待当前文件操作结束"));
    updateControls(scanTask_.state());
}

void FileOrganizePage::handleExecutionState(const TaskState state)
{
    executionState_ = state;
    updateControls(scanTask_.state());
    switch (state) {
    case TaskState::Idle:
        previewStatusLabel_->setText(QStringLiteral("尚未开始整理执行"));
        break;
    case TaskState::Preparing:
        previewStatusLabel_->setText(QStringLiteral("正在准备整理执行"));
        break;
    case TaskState::Running:
        previewStatusLabel_->setText(QStringLiteral("正在执行整理计划"));
        break;
    case TaskState::Cancelling:
        previewStatusLabel_->setText(
            QStringLiteral("正在取消整理执行，请等待最终结果"));
        break;
    case TaskState::Completed:
    case TaskState::CompletedWithErrors:
    case TaskState::Cancelled:
    case TaskState::Failed:
        break;
    }
}

void FileOrganizePage::handleExecutionItemProgress(
    const ExecutionProgressUpdate progress)
{
    executionCurrentFileLabel_->setText(
        QStringLiteral("当前项：%1 → %2\n动作：%3  阶段：%4")
            .arg(progress.sourcePath)
            .arg(progress.destinationPath)
            .arg(progress.action)
            .arg(progress.phase));
    executionSummaryLabel_->setText(
        ExecutionResultModel::summaryText(progress.summary));
    executionProgressBar_->setRange(
        0, static_cast<int>(std::max<qint64>(progress.total, 0)));
    executionProgressBar_->setValue(
        static_cast<int>(std::clamp<qint64>(
            progress.completed, 0, std::max<qint64>(progress.total, 0))));
}

void FileOrganizePage::handleExecutionProgress(
    const qint64 completed,
    const qint64 total,
    const QString currentFile)
{
    if (!currentFile.isEmpty()
        && !executionCurrentFileLabel_->text().contains(QStringLiteral("当前项："))) {
        executionCurrentFileLabel_->setText(
            QStringLiteral("当前文件：%1").arg(currentFile));
    }
    executionProgressBar_->setRange(
        0, static_cast<int>(std::max<qint64>(total, 0)));
    executionProgressBar_->setValue(
        static_cast<int>(std::clamp<qint64>(
            completed, 0, std::max<qint64>(total, 0))));
}

void FileOrganizePage::handleExecutionResult(const ExecutionResult result)
{
    planLocked_ = false;
    planConfirmed_ = false;
    executionState_ = result.cancelled
        ? TaskState::Cancelled
        : !result.fatalError.isEmpty()
            ? TaskState::Failed
            : result.summary.failed > 0
                || result.summary.rejected > 0
                || result.summary.sourceCleanupFailed > 0
                ? TaskState::CompletedWithErrors
                : TaskState::Completed;

    const qint64 processed = static_cast<qint64>(result.items.size());
    const qint64 total = std::max(result.summary.planned, processed);
    executionProgressBar_->setRange(0, static_cast<int>(total));
    executionProgressBar_->setValue(static_cast<int>(processed));
    executionSummaryLabel_->setText(
        ExecutionResultModel::summaryText(result.summary));
    executionResultModel_->setResult(result);
    executionCurrentFileLabel_->setText(QStringLiteral("当前项：执行结束"));

    QStringList executionErrors;
    for (const ExecutionItemResult &itemResult : result.items) {
        if (!itemResult.errorMessage.isEmpty()) {
            executionErrors << QStringLiteral("%1：%2")
                .arg(ExecutionResultModel::statusText(itemResult.status))
                .arg(itemResult.errorMessage);
        }
    }
    previewStatusLabel_->setToolTip(executionErrors.join(QStringLiteral("\n")));
    if (result.summary.sourceCleanupFailed > 0) {
        previewStatusLabel_->setText(
            result.cancelled
                ? QStringLiteral("整理执行已取消，但存在源文件清理失败项")
                : QStringLiteral("整理执行完成，但存在源文件清理失败项"));
    } else if (result.cancelled) {
        previewStatusLabel_->setText(QStringLiteral("整理执行已取消"));
    } else if (!result.fatalError.isEmpty()) {
        previewStatusLabel_->setText(
            QStringLiteral("整理执行失败：%1").arg(result.fatalError));
    } else if (result.summary.failed > 0 || result.summary.rejected > 0) {
        previewStatusLabel_->setText(QStringLiteral("整理执行部分完成"));
    } else {
        previewStatusLabel_->setText(QStringLiteral("整理执行完成"));
    }
    QString historyError;
    if (!application_.historyRepository().saveExecutionResult(
            executionContext_, executionState_, result, nullptr, &historyError)) {
        previewStatusLabel_->setText(
            previewStatusLabel_->text() + QStringLiteral("，但历史记录保存失败"));
        application_.logger().log(
            LogLevel::Warning,
            QStringLiteral("HistoryRepository"),
            QStringLiteral("Execution history save failed: %1").arg(historyError));
    }
    updateControls(scanTask_.state());
}

void FileOrganizePage::handleExecutionFailure(const QString message)
{
    planLocked_ = false;
    planConfirmed_ = false;
    executionState_ = TaskState::Failed;
    executionResultModel_->clear();
    ExecutionResult failedResult;
    failedResult.summary.planned = currentPlan_.plannedCount();
    failedResult.fatalError = message;
    QString historyError;
    if (!application_.historyRepository().saveExecutionResult(
            executionContext_, executionState_, failedResult, nullptr, &historyError)) {
        application_.logger().log(
            LogLevel::Warning,
            QStringLiteral("HistoryRepository"),
            QStringLiteral("Failed execution history save failed: %1")
                .arg(historyError));
    }
    executionProgressBar_->setRange(0, 100);
    executionProgressBar_->setValue(0);
    executionSummaryLabel_->setText(QStringLiteral("执行未产生完整结果"));
    previewStatusLabel_->setText(
        historyError.isEmpty()
            ? QStringLiteral("整理执行失败：%1").arg(message)
            : QStringLiteral("整理执行失败：%1，但历史记录保存失败")
                  .arg(message));
    updateControls(scanTask_.state());
}

void FileOrganizePage::handleExecutionCancelled()
{
    previewStatusLabel_->setText(
        QStringLiteral("整理执行已取消，正在汇总最终结果"));
}

void FileOrganizePage::updateControls(const TaskState state)
{
    const bool scanActive = state == TaskState::Preparing
        || state == TaskState::Running
        || state == TaskState::Cancelling;
    const bool executionActive =
        executionState_ == TaskState::Preparing
        || executionState_ == TaskState::Running
        || executionState_ == TaskState::Cancelling;
    const bool active = scanActive || planLocked_ || executionActive;

    directoryEdit_->setEnabled(!active);
    chooseDirectoryButton_->setEnabled(!active);
    scanButton_->setEnabled(!active);
    targetRootEdit_->setEnabled(!active);
    chooseTargetRootButton_->setEnabled(!active);
    generatePreviewButton_->setEnabled(!active && hasScanResult_);
    confirmPlanButton_->setEnabled(
        !active && planConfirmed_ && planMatchesCurrentInputs());
    cancelPlanButton_->setEnabled(
        !active && !currentPlan_.isEmpty() && !planLocked_);
    executePlanButton_->setEnabled(
        !active && planConfirmed_ && planMatchesCurrentInputs());
    cancelExecutionButton_->setEnabled(
        planLocked_
        && executionTask_.isActive()
        && (executionState_ == TaskState::Preparing
            || executionState_ == TaskState::Running));
}
void FileOrganizePage::updateSummary(
    const qint64 fileCount,
    const qint64 totalSizeBytes,
    const qint64 errorCount,
    const QHash<QString, qint64> &extensionCounts,
    const QHash<QString, qint64> &categoryCounts)
{
    fileCountValueLabel_->setText(QStringLiteral("文件：%1").arg(fileCount));
    totalSizeValueLabel_->setText(
        QStringLiteral("总大小：%1").arg(FileTableModel::formatFileSize(totalSizeBytes)));
    errorCountValueLabel_->setText(QStringLiteral("错误：%1").arg(errorCount));
    extensionStatsLabel_->setText(
        QStringLiteral("类型统计：%1").arg(countSummary(extensionCounts)));
    categoryStatsLabel_->setText(
        QStringLiteral("分类统计：%1").arg(countSummary(categoryCounts)));
}

QString FileOrganizePage::countSummary(
    const QHash<QString, qint64> &counts)
{
    if (counts.isEmpty()) {
        return QStringLiteral("暂无");
    }

    QList<QPair<QString, qint64>> values;
    values.reserve(counts.size());
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        values.append({it.key(), it.value()});
    }

    std::sort(values.begin(), values.end(),
              [](const QPair<QString, qint64> &left,
                 const QPair<QString, qint64> &right) {
                  return left.second == right.second
                      ? left.first < right.first
                      : left.second > right.second;
              });

    QStringList parts;
    for (int index = 0; index < values.size(); ++index) {
        const auto &value = values.at(index);
        const QString label = value.first.isEmpty()
            ? QStringLiteral("(empty)")
            : value.first;
        parts << QStringLiteral("%1 %2").arg(label).arg(value.second);
    }

    return parts.join(QStringLiteral(" · "));
}

} // namespace FilePilot

