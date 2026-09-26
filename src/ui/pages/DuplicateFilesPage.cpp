#include "ui/pages/DuplicateFilesPage.h"

#include "core/duplicates/DuplicateTypes.h"
#include "ui/models/DuplicateModels.h"

#include <QAbstractItemView>
#include <QBoxLayout>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QResizeEvent>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTableView>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <climits>

namespace FilePilot {

namespace {

QLabel *addMetric(QWidget *parent,
                  QBoxLayout *layout,
                  const QString &title,
                  const QString &value,
                  const QString &objectName)
{
    auto *metric = new QWidget(parent);
    metric->setObjectName(objectName + QStringLiteral("Metric"));
    metric->setMinimumWidth(116);
    auto *metricLayout = new QVBoxLayout(metric);
    metricLayout->setContentsMargins(0, 2, 0, 2);
    metricLayout->setSpacing(2);

    auto *titleLabel = new QLabel(title, metric);
    titleLabel->setObjectName(objectName + QStringLiteral("Label"));
    titleLabel->setProperty("duplicateMetricLabel", true);
    titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    metricLayout->addWidget(titleLabel);

    auto *valueLabel = new QLabel(value, metric);
    valueLabel->setObjectName(objectName);
    valueLabel->setMinimumWidth(108);
    valueLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    metricLayout->addWidget(valueLabel);

    layout->addWidget(metric);
    return valueLabel;
}

void addSummaryDivider(QWidget *parent, QBoxLayout *layout)
{
    auto *divider = new QFrame(parent);
    divider->setObjectName(QStringLiteral("duplicateSummaryDivider"));
    divider->setFrameShape(QFrame::NoFrame);
    divider->setFixedWidth(1);
    layout->addWidget(divider);
}

void configureResultTable(QTableView *table)
{
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);
    table->setSortingEnabled(false);
    table->setWordWrap(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setShowGrid(false);
    table->setFocusPolicy(Qt::StrongFocus);
    table->setTabKeyNavigation(true);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(34);
    table->horizontalHeader()->setMinimumSectionSize(64);
    table->horizontalHeader()->setStretchLastSection(true);
}

QString duplicatePhaseDisplay(const DuplicatePhase phase)
{
    switch (phase) {
    case DuplicatePhase::CandidateFiltering:
        return QStringLiteral("筛选候选文件");
    case DuplicatePhase::PartialHashing:
        return QStringLiteral("计算部分 Hash");
    case DuplicatePhase::FullHashing:
        return QStringLiteral("计算完整 Hash");
    case DuplicatePhase::Completed:
        return QStringLiteral("已完成");
    case DuplicatePhase::Cancelled:
        return QStringLiteral("已取消");
    case DuplicatePhase::Failed:
        return QStringLiteral("失败");
    }
    return {};
}

} // namespace

DuplicateFilesPage::DuplicateFilesPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("pageDuplicates"));
    setWindowTitle(QStringLiteral("重复文件"));
    buildUi();

    connect(&scanTask_, &ScanTask::stateChanged,
            this, &DuplicateFilesPage::handleScanState);
    connect(&scanTask_, &ScanTask::progressChanged,
            this, &DuplicateFilesPage::handleScanProgress);
    connect(&scanTask_, &ScanTask::errorBatchReported,
            this, &DuplicateFilesPage::handleScanErrorBatch);
    connect(&scanTask_, &ScanTask::completed,
            this, &DuplicateFilesPage::handleScanCompleted);
    connect(&scanTask_, &ScanTask::failed,
            this, &DuplicateFilesPage::handleScanFailure);
    connect(&scanTask_, &ScanTask::cancelled,
            this, &DuplicateFilesPage::handleScanCancelled);

    connect(&duplicateTask_, &DuplicateTask::stateChanged,
            this, &DuplicateFilesPage::handleDuplicateState);
    connect(&duplicateTask_, &DuplicateTask::progressChanged,
            this, &DuplicateFilesPage::handleDuplicateProgress);
    connect(&duplicateTask_, &DuplicateTask::errorReported,
            this, &DuplicateFilesPage::handleDuplicateError);
    connect(&duplicateTask_, &DuplicateTask::completed,
            this, &DuplicateFilesPage::handleDuplicateCompleted);
    connect(&duplicateTask_, &DuplicateTask::failed,
            this, &DuplicateFilesPage::handleDuplicateFailure);
    connect(&duplicateTask_, &DuplicateTask::cancelled,
            this, &DuplicateFilesPage::handleDuplicateCancelled);

    updateSummary(DuplicateSummary{});
    updateControls();
}

void DuplicateFilesPage::buildUi()
{
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(32, 24, 32, 22);
    rootLayout->setSpacing(16);

    auto *titleBlock = new QWidget(this);
    titleBlock->setObjectName(QStringLiteral("duplicateTitleBlock"));
    auto *titleLayout = new QVBoxLayout(titleBlock);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(2);

    auto *titleLabel = new QLabel(QStringLiteral("重复文件"), titleBlock);
    titleLabel->setObjectName(QStringLiteral("duplicateTitle"));
    titleLayout->addWidget(titleLabel);

    auto *subtitleLabel = new QLabel(
        QStringLiteral("查找内容完全相同的文件。"),
        titleBlock);
    subtitleLabel->setObjectName(QStringLiteral("duplicateSubtitle"));
    subtitleLabel->setWordWrap(true);
    titleLayout->addWidget(subtitleLabel);
    rootLayout->addWidget(titleBlock);

    auto *directoryRow = new QHBoxLayout();
    directoryRow->setSpacing(8);

    auto *directoryLabel = new QLabel(QStringLiteral("扫描位置"), this);
    directoryLabel->setObjectName(QStringLiteral("duplicateDirectoryLabel"));
    directoryLabel->setMinimumWidth(58);
    directoryRow->addWidget(directoryLabel);

    directoryEdit_ = new QLineEdit(this);
    directoryEdit_->setObjectName(QStringLiteral("duplicateDirectoryEdit"));
    directoryEdit_->setPlaceholderText(QStringLiteral("选择要扫描的本地目录"));
    directoryEdit_->setClearButtonEnabled(true);
    directoryEdit_->setAccessibleName(QStringLiteral("扫描目录"));
    directoryRow->addWidget(directoryEdit_, 1);

    chooseDirectoryButton_ = new QPushButton(QStringLiteral("选择目录"), this);
    chooseDirectoryButton_->setObjectName(
        QStringLiteral("chooseDuplicateDirectoryButton"));
    chooseDirectoryButton_->setToolTip(QStringLiteral("选择要扫描的目录"));
    directoryRow->addWidget(chooseDirectoryButton_);

    scanButton_ = new QPushButton(QStringLiteral("扫描重复文件"), this);
    scanButton_->setObjectName(QStringLiteral("startDuplicateScanButton"));
    scanButton_->setDefault(true);
    scanButton_->setToolTip(QStringLiteral("扫描目录并查找内容相同的文件"));
    directoryRow->addWidget(scanButton_);
    rootLayout->addLayout(directoryRow);

    auto *summaryPanel = new QFrame(this);
    summaryPanel->setObjectName(QStringLiteral("duplicateSummaryPanel"));
    summaryPanel->setFrameShape(QFrame::NoFrame);
    auto *summaryLayout = new QHBoxLayout(summaryPanel);
    summaryLayout->setContentsMargins(0, 8, 0, 8);
    summaryLayout->setSpacing(22);

    groupCountValueLabel_ = addMetric(
        summaryPanel,
        summaryLayout,
        QStringLiteral("重复组"),
        QStringLiteral("0"),
        QStringLiteral("duplicateGroupCountValue"));
    addSummaryDivider(summaryPanel, summaryLayout);
    duplicateFileCountValueLabel_ = addMetric(
        summaryPanel,
        summaryLayout,
        QStringLiteral("重复文件"),
        QStringLiteral("0"),
        QStringLiteral("duplicateFileCountValue"));
    addSummaryDivider(summaryPanel, summaryLayout);
    duplicateSizeValueLabel_ = addMetric(
        summaryPanel,
        summaryLayout,
        QStringLiteral("占用空间"),
        duplicateFileSizeText(0),
        QStringLiteral("duplicateSizeValue"));
    addSummaryDivider(summaryPanel, summaryLayout);
    wastedSizeValueLabel_ = addMetric(
        summaryPanel,
        summaryLayout,
        QStringLiteral("可回收空间"),
        duplicateFileSizeText(0),
        QStringLiteral("duplicateWastedSizeValue"));
    summaryLayout->addStretch(1);
    rootLayout->addWidget(summaryPanel);

    auto *progressPanel = new QFrame(this);
    progressPanel->setObjectName(QStringLiteral("duplicateProgressPanel"));
    progressPanel->setFrameShape(QFrame::NoFrame);
    auto *progressLayout = new QVBoxLayout(progressPanel);
    progressLayout->setContentsMargins(0, 6, 0, 10);
    progressLayout->setSpacing(8);

    auto *progressHeader = new QHBoxLayout();
    progressHeader->setSpacing(12);

    stageLabel_ = new QLabel(QStringLiteral("当前阶段：等待开始"), progressPanel);
    stageLabel_->setObjectName(QStringLiteral("duplicateStageLabel"));
    progressHeader->addWidget(stageLabel_);

    currentFileLabel_ = new QLabel(QStringLiteral("当前文件：无"), progressPanel);
    currentFileLabel_->setObjectName(QStringLiteral("duplicateCurrentFileLabel"));
    currentFileLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    progressHeader->addWidget(currentFileLabel_, 1);

    progressValueLabel_ = new QLabel(QStringLiteral("0 / 0"), progressPanel);
    progressValueLabel_->setObjectName(QStringLiteral("duplicateProgressValue"));
    progressHeader->addWidget(progressValueLabel_);

    cancelButton_ = new QPushButton(QStringLiteral("取消"), progressPanel);
    cancelButton_->setObjectName(QStringLiteral("cancelDuplicateScanButton"));
    cancelButton_->setToolTip(QStringLiteral("取消当前扫描"));
    progressHeader->addWidget(cancelButton_);
    progressLayout->addLayout(progressHeader);

    progressBar_ = new QProgressBar(progressPanel);
    progressBar_->setObjectName(QStringLiteral("duplicateProgressBar"));
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    progressBar_->setTextVisible(false);
    progressLayout->addWidget(progressBar_);
    rootLayout->addWidget(progressPanel);

    statusLabel_ = new QLabel(QStringLiteral("选择目录后开始扫描"), this);
    statusLabel_->setObjectName(QStringLiteral("duplicateStatusLabel"));
    statusLabel_->setWordWrap(true);
    statusLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rootLayout->addWidget(statusLabel_);

    resultsStack_ = new QStackedWidget(this);
    resultsStack_->setObjectName(QStringLiteral("duplicateResultsStack"));

    auto *emptyWidget = new QWidget(resultsStack_);
    auto *emptyLayout = new QVBoxLayout(emptyWidget);
    emptyLayout->setContentsMargins(24, 24, 24, 24);

    emptyStateLabel_ = new QLabel(
        QStringLiteral("等待扫描\n\n选择扫描目录后开始查找重复文件。"),
        emptyWidget);
    emptyStateLabel_->setObjectName(QStringLiteral("duplicateEmptyStateLabel"));
    emptyStateLabel_->setAlignment(Qt::AlignCenter);
    emptyStateLabel_->setWordWrap(true);
    emptyLayout->addWidget(emptyStateLabel_);
    resultsStack_->addWidget(emptyWidget);

    auto *resultsWidget = new QWidget(resultsStack_);
    auto *resultsLayout = new QVBoxLayout(resultsWidget);
    resultsLayout->setContentsMargins(0, 0, 0, 0);
    resultsLayout->setSpacing(8);

    resultsSplitter_ = new QSplitter(Qt::Horizontal, resultsWidget);
    resultsSplitter_->setObjectName(QStringLiteral("duplicateResultsSplitter"));
    resultsSplitter_->setChildrenCollapsible(false);
    resultsSplitter_->setHandleWidth(1);
    resultsSplitter_->setOpaqueResize(true);

    auto *groupPanel = new QWidget(resultsSplitter_);
    auto *groupLayout = new QVBoxLayout(groupPanel);
    groupLayout->setContentsMargins(0, 0, 4, 0);
    groupLayout->setSpacing(8);

    auto *groupTitle = new QLabel(QStringLiteral("重复组"), groupPanel);
    groupTitle->setObjectName(QStringLiteral("duplicateGroupTitle"));
    groupLayout->addWidget(groupTitle);

    groupTableView_ = new QTableView(groupPanel);
    groupTableView_->setObjectName(QStringLiteral("duplicateGroupTableView"));
    groupModel_ = new DuplicateGroupModel(groupTableView_);
    groupTableView_->setModel(groupModel_);
    configureResultTable(groupTableView_);
    groupTableView_->setColumnWidth(DuplicateGroupModel::Group, 66);
    groupTableView_->setColumnWidth(DuplicateGroupModel::FileCount, 94);
    groupTableView_->setColumnWidth(DuplicateGroupModel::FileSize, 112);
    groupTableView_->setColumnWidth(DuplicateGroupModel::WastedSize, 124);
    groupTableView_->horizontalHeader()->setSectionResizeMode(
        DuplicateGroupModel::Summary,
        QHeaderView::Stretch);
    groupLayout->addWidget(groupTableView_, 1);
    resultsSplitter_->addWidget(groupPanel);

    auto *detailsPanel = new QWidget(resultsSplitter_);
    auto *detailsLayout = new QVBoxLayout(detailsPanel);
    detailsLayout->setContentsMargins(4, 0, 0, 0);
    detailsLayout->setSpacing(8);

    auto *detailTitle = new QLabel(QStringLiteral("所选组详情"), detailsPanel);
    detailTitle->setObjectName(QStringLiteral("duplicateDetailTitle"));
    detailsLayout->addWidget(detailTitle);

    detailTabs_ = new QTabWidget(detailsPanel);
    detailTabs_->setObjectName(QStringLiteral("duplicateDetailTabs"));
    detailTabs_->setDocumentMode(true);

    itemTableView_ = new QTableView(detailTabs_);
    itemTableView_->setObjectName(QStringLiteral("duplicateItemTableView"));
    itemModel_ = new DuplicateItemModel(itemTableView_);
    itemTableView_->setModel(itemModel_);
    configureResultTable(itemTableView_);
    itemTableView_->setColumnWidth(DuplicateItemModel::Path, 360);
    itemTableView_->setColumnWidth(DuplicateItemModel::Size, 96);
    itemTableView_->setColumnWidth(DuplicateItemModel::ModifiedTime, 156);
    itemTableView_->setColumnWidth(DuplicateItemModel::HashStatus, 112);
    itemTableView_->horizontalHeader()->setSectionResizeMode(
        DuplicateItemModel::Path,
        QHeaderView::Stretch);
    detailTabs_->addTab(itemTableView_, QStringLiteral("文件详情"));

    errorTableView_ = new QTableView(detailTabs_);
    errorTableView_->setObjectName(QStringLiteral("duplicateErrorTableView"));
    errorModel_ = new DuplicateErrorModel(errorTableView_);
    errorTableView_->setModel(errorModel_);
    configureResultTable(errorTableView_);
    errorTableView_->setColumnWidth(DuplicateErrorModel::Path, 360);
    errorTableView_->setColumnWidth(DuplicateErrorModel::Code, 76);
    errorTableView_->horizontalHeader()->setSectionResizeMode(
        DuplicateErrorModel::Path,
        QHeaderView::Stretch);
    detailTabs_->addTab(errorTableView_, QStringLiteral("错误信息"));

    detailsLayout->addWidget(detailTabs_, 1);
    resultsSplitter_->addWidget(detailsPanel);
    resultsLayout->addWidget(resultsSplitter_, 1);
    resultsStack_->addWidget(resultsWidget);
    rootLayout->addWidget(resultsStack_, 1);

    connect(chooseDirectoryButton_, &QPushButton::clicked,
            this, &DuplicateFilesPage::chooseDirectory);
    connect(scanButton_, &QPushButton::clicked,
            this, &DuplicateFilesPage::startScan);
    connect(cancelButton_, &QPushButton::clicked,
            this, &DuplicateFilesPage::cancelScan);
    connect(directoryEdit_, &QLineEdit::returnPressed,
            this, &DuplicateFilesPage::startScan);
    connect(directoryEdit_, &QLineEdit::textChanged,
            this, [this](const QString &) { updateControls(); });
    connect(groupTableView_->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this,
            [this](const QItemSelection &, const QItemSelection &) {
                showSelectedGroup();
            });

    updateResultsSplitterOrientation();
}
void DuplicateFilesPage::chooseDirectory()
{
    if (busy_ || scanTask_.isActive() || duplicateTask_.isActive()) {
        return;
    }

    const QString initialDirectory = directoryEdit_->text().trimmed();
    const QString directory = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("选择扫描目录"),
        initialDirectory);

    if (!directory.isEmpty()) {
        directoryEdit_->setText(directory);
    }
}

void DuplicateFilesPage::startScan()
{
    if (busy_) {
        return;
    }

    const QString rootPath = directoryEdit_->text().trimmed();
    if (rootPath.isEmpty()) {
        setStatusText(QStringLiteral("请选择要扫描的目录。"), true);
        directoryEdit_->setFocus();
        return;
    }

    scanErrors_.clear();
    duplicateErrors_.clear();
    updateSummary(DuplicateSummary{});
    resetResults(
        QStringLiteral("正在准备扫描"),
        QStringLiteral("正在收集文件信息，请稍候。"));
    stage_ = ScanStage::Inventory;
    busy_ = true;
    cancelRequested_ = false;
    progressBar_->setRange(0, 0);
    progressBar_->setValue(0);
    progressValueLabel_->setText(QStringLiteral("准备中"));
    setStageText(QStringLiteral("收集文件信息"), QString());
    setStatusText(QStringLiteral("正在扫描文件…"));
    updateControls();

    if (!scanTask_.start(rootPath)) {
        handleScanFailure(QStringLiteral("文件扫描任务当前不可用。"));
    }
}

void DuplicateFilesPage::cancelScan()
{
    if (!busy_) {
        return;
    }

    cancelRequested_ = true;
    if (scanTask_.isActive()) {
        scanTask_.cancel();
    } else if (duplicateTask_.isActive()) {
        duplicateTask_.cancel();
    }
    setStatusText(QStringLiteral("正在取消扫描…"));
    updateControls();
}

void DuplicateFilesPage::handleScanState(const TaskState state)
{
    if (stage_ != ScanStage::Inventory) {
        return;
    }

    if (state == TaskState::Cancelling) {
        setStageText(QStringLiteral("正在取消"), QString());
    } else if (state == TaskState::Preparing) {
        setStageText(QStringLiteral("准备扫描"), QString());
    } else if (state == TaskState::Running) {
        setStageText(QStringLiteral("收集文件信息"), QString());
    }
    updateControls();
}

void DuplicateFilesPage::handleScanProgress(const qint64 scannedFileCount,
                                            QString currentDirectory,
                                            QString currentFile)
{
    if (stage_ != ScanStage::Inventory) {
        return;
    }

    QString currentPath = currentFile;
    if (currentPath.isEmpty()) {
        currentPath = currentDirectory;
    }
    setStageText(QStringLiteral("收集文件信息"), currentPath);
    progressBar_->setRange(0, 0);
    progressValueLabel_->setText(
        QStringLiteral("已发现 %1 个文件").arg(scannedFileCount));
    setStatusText(QStringLiteral("正在扫描文件…（已发现 %1 个）")
                      .arg(scannedFileCount));
}

void DuplicateFilesPage::handleScanErrorBatch(const ScanErrorBatch batch)
{
    if (stage_ != ScanStage::Inventory) {
        return;
    }

    scanErrors_.insert(
        scanErrors_.end(),
        batch.errors.begin(),
        batch.errors.end());
    rebuildErrorModel();
    setStatusText(
        QStringLiteral("正在扫描文件…（已发现 %1 个错误）")
            .arg(batch.totalErrorCount));
}

void DuplicateFilesPage::handleScanCompleted(ScanResult result)
{
    if (result.cancelled) {
        return;
    }
    if (!result.completed) {
        handleScanFailure(
            result.fatalError.isEmpty()
                ? QStringLiteral("文件扫描未完成。")
                : result.fatalError);
        return;
    }

    scanErrors_ = result.errors;
    startDuplicateHashing(result);
}

void DuplicateFilesPage::handleScanFailure(const QString message)
{
    busy_ = false;
    stage_ = ScanStage::Idle;
    cancelRequested_ = false;
    updateControls();
    updateSummary(DuplicateSummary{});
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    progressValueLabel_->setText(QStringLiteral("0 / 0"));
    setStageText(QStringLiteral("失败"), QString());
    resetResults(QStringLiteral("扫描失败"), message);
    setStatusText(QStringLiteral("扫描失败：%1").arg(message), true);
}

void DuplicateFilesPage::handleScanCancelled()
{
    busy_ = false;
    stage_ = ScanStage::Idle;
    cancelRequested_ = false;
    updateControls();
    updateSummary(DuplicateSummary{});
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    progressValueLabel_->setText(QStringLiteral("0 / 0"));
    setStageText(QStringLiteral("已取消"), QString());
    resetResults(
        QStringLiteral("扫描已取消"),
        QStringLiteral("本次扫描已取消，没有生成新的重复文件结果。"));
    setStatusText(QStringLiteral("扫描已取消。"));
}

void DuplicateFilesPage::startDuplicateHashing(const ScanResult &scanResult)
{
    stage_ = ScanStage::DuplicateHashing;
    cancelRequested_ = false;
    duplicateErrors_.clear();
    rebuildErrorModel();
    progressBar_->setRange(0, 0);
    progressBar_->setValue(0);
    progressValueLabel_->setText(QStringLiteral("0 / 0"));
    setStageText(QStringLiteral("准备计算 Hash"), QString());
    setStatusText(QStringLiteral("正在扫描重复文件…"));
    updateControls();

    if (!duplicateTask_.start(scanResult)) {
        handleDuplicateFailure(QStringLiteral("重复文件检测任务当前不可用。"));
    }
}

void DuplicateFilesPage::handleDuplicateState(const TaskState state)
{
    if (stage_ != ScanStage::DuplicateHashing) {
        return;
    }

    if (state == TaskState::Cancelling) {
        setStageText(QStringLiteral("正在取消"), QString());
    } else if (state == TaskState::Preparing) {
        setStageText(QStringLiteral("准备计算 Hash"), QString());
    }
    updateControls();
}

void DuplicateFilesPage::handleDuplicateProgress(const DuplicateProgress progress)
{
    if (stage_ != ScanStage::DuplicateHashing) {
        return;
    }

    setStageText(duplicatePhaseDisplay(progress.phase), progress.currentPath);
    if (progress.total > 0) {
        progressBar_->setRange(0, static_cast<int>(std::min<qint64>(
                                      progress.total,
                                      static_cast<qint64>(INT_MAX))));
        progressBar_->setValue(static_cast<int>(std::min<qint64>(
            progress.processed,
            static_cast<qint64>(INT_MAX))));
        progressValueLabel_->setText(
            QStringLiteral("%1 / %2")
                .arg(progress.processed)
                .arg(progress.total));
    } else {
        progressBar_->setRange(0, 0);
        progressValueLabel_->setText(QStringLiteral("处理中"));
    }
    setStatusText(QStringLiteral("正在扫描重复文件…"));
}

void DuplicateFilesPage::handleDuplicateError(const DuplicateError error)
{
    if (stage_ != ScanStage::DuplicateHashing) {
        return;
    }

    duplicateErrors_.push_back(error);
    rebuildErrorModel();
    setStatusText(
        QStringLiteral("正在扫描重复文件…（%1：%2）")
            .arg(error.path, error.message),
        true);
}

void DuplicateFilesPage::handleDuplicateCompleted(DuplicateResult result)
{
    if (result.cancelled) {
        return;
    }
    if (!result.completed) {
        handleDuplicateFailure(
            result.fatalError.isEmpty()
                ? QStringLiteral("重复文件检测未完成。")
                : result.fatalError);
        return;
    }

    busy_ = false;
    stage_ = ScanStage::Idle;
    cancelRequested_ = false;
    setStageText(QStringLiteral("已完成"), QString());
    showDuplicateResult(result);
    updateControls();
}

void DuplicateFilesPage::handleDuplicateFailure(const QString message)
{
    busy_ = false;
    stage_ = ScanStage::Idle;
    cancelRequested_ = false;
    updateControls();
    updateSummary(DuplicateSummary{});
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    progressValueLabel_->setText(QStringLiteral("0 / 0"));
    setStageText(QStringLiteral("失败"), QString());
    resetResults(QStringLiteral("重复文件检测失败"), message);
    setStatusText(QStringLiteral("重复文件检测失败：%1").arg(message), true);
}

void DuplicateFilesPage::handleDuplicateCancelled()
{
    busy_ = false;
    stage_ = ScanStage::Idle;
    cancelRequested_ = false;
    updateControls();
    updateSummary(DuplicateSummary{});
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    progressValueLabel_->setText(QStringLiteral("0 / 0"));
    setStageText(QStringLiteral("已取消"), QString());
    resetResults(
        QStringLiteral("扫描已取消"),
        QStringLiteral("本次扫描已取消，没有生成新的重复文件结果。"));
    setStatusText(QStringLiteral("扫描已取消。"));
}

void DuplicateFilesPage::showDuplicateResult(const DuplicateResult &result)
{
    duplicateErrors_ = result.errors;
    rebuildErrorModel();
    updateSummary(result.summary);
    groupModel_->setGroups(result.groups);

    const bool hasErrors = !scanErrors_.empty() || !duplicateErrors_.empty();
    if (result.groups.empty() && !hasErrors) {
        itemModel_->clear();
        resetResults(
            QStringLiteral("没有发现重复文件"),
            QStringLiteral("当前扫描范围内没有找到内容完全相同的文件。"));
        setStatusText(QStringLiteral("扫描完成，没有发现重复文件。"));
        return;
    }

    resultsStack_->setCurrentIndex(1);
    detailTabs_->setTabText(
        1,
        QStringLiteral("错误信息（%1）").arg(errorModel_->rowCount()));

    if (!result.groups.empty()) {
        const QModelIndex first = groupModel_->index(0, 0);
        groupTableView_->selectionModel()->select(
            first,
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        groupTableView_->setCurrentIndex(first);
        showSelectedGroup();
    } else {
        itemModel_->clear();
        detailTabs_->setCurrentIndex(1);
    }

    const qint64 totalErrorCount =
        static_cast<qint64>(scanErrors_.size()) + result.summary.errorCount;
    if (totalErrorCount > 0) {
        setStatusText(
            QStringLiteral("扫描完成：发现 %1 个重复组，%2 个错误。")
                .arg(result.summary.groupCount)
                .arg(totalErrorCount),
            true);
    } else {
        setStatusText(
            QStringLiteral("扫描完成：发现 %1 个重复组，可回收 %2。")
                .arg(result.summary.groupCount)
                .arg(duplicateFileSizeText(result.summary.wastedBytes)));
    }
}

void DuplicateFilesPage::showSelectedGroup()
{
    const QModelIndexList selected =
        groupTableView_->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        itemModel_->clear();
        return;
    }

    const DuplicateGroup *group = groupModel_->groupAt(selected.first().row());
    if (group == nullptr) {
        itemModel_->clear();
        return;
    }

    itemModel_->setItems(group->items);
    detailTabs_->setCurrentIndex(0);
}

void DuplicateFilesPage::updateSummary(const DuplicateSummary &summary)
{
    groupCountValueLabel_->setText(QString::number(summary.groupCount));
    duplicateFileCountValueLabel_->setText(
        QString::number(summary.duplicateFileCount));
    duplicateSizeValueLabel_->setText(
        duplicateFileSizeText(summary.duplicateBytes));
    wastedSizeValueLabel_->setText(
        duplicateFileSizeText(summary.wastedBytes));
}

void DuplicateFilesPage::updateControls()
{
    const bool active = busy_
        || scanTask_.isActive()
        || duplicateTask_.isActive();
    chooseDirectoryButton_->setEnabled(!active);
    directoryEdit_->setEnabled(!active);
    scanButton_->setEnabled(
        !active && !directoryEdit_->text().trimmed().isEmpty());
    cancelButton_->setEnabled(active && !cancelRequested_);
}

void DuplicateFilesPage::updateResultsSplitterOrientation()
{
    if (resultsSplitter_ == nullptr) {
        return;
    }

    const Qt::Orientation orientation =
        width() < 900 ? Qt::Vertical : Qt::Horizontal;
    resultsSplitter_->setOrientation(orientation);
    if (orientation == Qt::Horizontal) {
        resultsSplitter_->setSizes({360, width() - 360});
    } else {
        resultsSplitter_->setSizes({280, qMax(280, height() - 280)});
    }
}

void DuplicateFilesPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateResultsSplitterOrientation();
}

void DuplicateFilesPage::setStageText(const QString &stage,
                                      const QString &currentFile)
{
    stageLabel_->setText(QStringLiteral("当前阶段：%1").arg(stage));
    currentFileLabel_->setText(
        currentFile.isEmpty()
            ? QStringLiteral("当前文件：无")
            : QStringLiteral("当前文件：%1").arg(currentFile));
    currentFileLabel_->setToolTip(currentFile);
}

void DuplicateFilesPage::setStatusText(const QString &message,
                                       const bool isError)
{
    statusLabel_->setText(message);
    statusLabel_->setProperty("errorState", isError);
    statusLabel_->style()->unpolish(statusLabel_);
    statusLabel_->style()->polish(statusLabel_);
}

void DuplicateFilesPage::resetResults(const QString &title,
                                      const QString &message)
{
    groupModel_->clear();
    itemModel_->clear();
    rebuildErrorModel();
    emptyStateLabel_->setText(title + QStringLiteral("\n\n") + message);
    if (errorModel_->rowCount() > 0) {
        resultsStack_->setCurrentIndex(1);
        detailTabs_->setCurrentIndex(1);
    } else {
        resultsStack_->setCurrentIndex(0);
    }
}

void DuplicateFilesPage::rebuildErrorModel()
{
    std::vector<DuplicateError> errors;
    errors.reserve(scanErrors_.size() + duplicateErrors_.size());
    for (const ScanError &error : scanErrors_) {
        errors.push_back(DuplicateError{error.path, error.message, error.code});
    }
    errors.insert(
        errors.end(),
        duplicateErrors_.begin(),
        duplicateErrors_.end());
    errorModel_->setErrors(std::move(errors));
    if (detailTabs_ != nullptr) {
        detailTabs_->setTabText(
            1,
            QStringLiteral("错误信息（%1）").arg(errorModel_->rowCount()));
    }
}

} // namespace FilePilot
