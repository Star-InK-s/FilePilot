#include "ui/pages/FileOrganizePage.h"

#include "app/Application.h"
#include "ui/models/FileTableModel.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
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
    connect(&scanTask_, &ScanTask::errorReported,
            this, &FileOrganizePage::handleError);
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
    rootLayout->setContentsMargins(28, 24, 28, 22);
    rootLayout->setSpacing(16);

    auto *titleLabel = new QLabel(QStringLiteral("文件整理"), this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(16);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    rootLayout->addWidget(titleLabel);

    auto *directoryRow = new QHBoxLayout();
    directoryRow->setSpacing(10);

    auto *directoryLabel = new QLabel(QStringLiteral("扫描目录"), this);
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
    directoryRow->addWidget(chooseDirectoryButton_);

    scanButton_ = new QPushButton(
        style()->standardIcon(QStyle::SP_BrowserReload),
        QStringLiteral("开始扫描"),
        this);
    scanButton_->setObjectName(QStringLiteral("scanButton"));
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
    summaryRow->addWidget(extensionStatsLabel_, 1);
    rootLayout->addLayout(summaryRow);

    scanStatusLabel_ = new QLabel(QStringLiteral("请选择目录后开始扫描"), this);
    scanStatusLabel_->setObjectName(QStringLiteral("scanStatusLabel"));
    scanStatusLabel_->setWordWrap(true);
    rootLayout->addWidget(scanStatusLabel_);

    fileTableView_ = new QTableView(this);
    fileTableView_->setObjectName(QStringLiteral("fileTableView"));
    fileModel_ = new FileTableModel(fileTableView_);
    fileTableView_->setModel(fileModel_);
    fileTableView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    fileTableView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    fileTableView_->setAlternatingRowColors(true);
    fileTableView_->setSortingEnabled(false);
    fileTableView_->verticalHeader()->setVisible(false);
    fileTableView_->horizontalHeader()->setStretchLastSection(true);
    fileTableView_->setColumnWidth(FileTableModel::FileName, 220);
    fileTableView_->setColumnWidth(FileTableModel::Type, 120);
    fileTableView_->setColumnWidth(FileTableModel::Size, 110);
    fileTableView_->setColumnWidth(FileTableModel::ModifiedTime, 150);
    rootLayout->addWidget(fileTableView_, 1);

    connect(chooseDirectoryButton_, &QPushButton::clicked,
            this, &FileOrganizePage::chooseDirectory);
    connect(scanButton_, &QPushButton::clicked,
            this, &FileOrganizePage::startScan);
    connect(directoryEdit_, &QLineEdit::returnPressed,
            this, &FileOrganizePage::startScan);
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
        scanStatusLabel_->setText(QStringLiteral("请先选择扫描目录"));
        return;
    }

    if (scanTask_.isActive()) {
        return;
    }

    currentRoot_ = rootPath;
    errorCount_ = 0;
    fileModel_->clear();
    updateSummary(0, 0, 0, {});
    scanStatusLabel_->setText(QStringLiteral("正在准备扫描"));
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

void FileOrganizePage::handleTaskState(const TaskState state)
{
    updateControls(state);
    emit taskStateChanged(state);

    switch (state) {
    case TaskState::Preparing:
        scanStatusLabel_->setText(QStringLiteral("正在准备扫描"));
        break;
    case TaskState::Running:
        scanStatusLabel_->setText(QStringLiteral("正在扫描文件"));
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
        QStringLiteral("已扫描 %1 个文件").arg(scannedFileCount));
    emit taskProgressChanged(scannedFileCount, currentDirectory, currentFile);
}

void FileOrganizePage::handleError(const ScanError error)
{
    Q_UNUSED(error);
    ++errorCount_;
    errorCountValueLabel_->setText(QStringLiteral("错误：%1").arg(errorCount_));
    emit taskErrorCountChanged(errorCount_);
}

void FileOrganizePage::handleCompleted(const ScanResult result)
{
    errorCount_ = result.statistics.errorCount;
    fileModel_->setFiles(result.files);
    updateSummary(
        result.statistics.fileCount,
        result.statistics.totalSizeBytes,
        result.statistics.errorCount,
        result.statistics.extensionCounts);

    QStringList errorDetails;
    for (const ScanError &error : result.errors) {
        errorDetails << QStringLiteral("%1：%2").arg(error.path, error.message);
        if (errorDetails.size() >= 5) {
            break;
        }
    }
    scanStatusLabel_->setToolTip(errorDetails.join(QStringLiteral("\n")));

    scanStatusLabel_->setText(result.statistics.errorCount > 0
        ? QStringLiteral("扫描完成，发现 %1 个错误")
              .arg(result.statistics.errorCount)
        : QStringLiteral("扫描完成"));
    emit taskProgressChanged(
        result.statistics.fileCount, result.rootPath, QString());
}

void FileOrganizePage::handleFailed(const QString message)
{
    fileModel_->clear();
    updateSummary(0, 0, errorCount_, {});
    scanStatusLabel_->setText(QStringLiteral("扫描失败：%1").arg(message));
}

void FileOrganizePage::handleCancelled()
{
    scanStatusLabel_->setText(QStringLiteral("扫描已取消"));
}

void FileOrganizePage::updateControls(const TaskState state)
{
    const bool active = state == TaskState::Preparing
        || state == TaskState::Running
        || state == TaskState::Cancelling;

    directoryEdit_->setEnabled(!active);
    chooseDirectoryButton_->setEnabled(!active);
    scanButton_->setEnabled(!active);
}

void FileOrganizePage::updateSummary(
    const qint64 fileCount,
    const qint64 totalSizeBytes,
    const qint64 errorCount,
    const QHash<QString, qint64> &extensionCounts)
{
    fileCountValueLabel_->setText(QStringLiteral("文件：%1").arg(fileCount));
    totalSizeValueLabel_->setText(
        QStringLiteral("总大小：%1").arg(FileTableModel::formatFileSize(totalSizeBytes)));
    errorCountValueLabel_->setText(QStringLiteral("错误：%1").arg(errorCount));
    extensionStatsLabel_->setText(
        QStringLiteral("类型统计：%1").arg(extensionSummary(extensionCounts)));
}

QString FileOrganizePage::extensionSummary(
    const QHash<QString, qint64> &extensionCounts)
{
    if (extensionCounts.isEmpty()) {
        return QStringLiteral("暂无");
    }

    QList<QPair<QString, qint64>> values;
    values.reserve(extensionCounts.size());
    for (auto it = extensionCounts.cbegin(); it != extensionCounts.cend(); ++it) {
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
    for (int index = 0; index < values.size() && index < 5; ++index) {
        const auto &value = values.at(index);
        parts << QStringLiteral("%1 %2").arg(value.first).arg(value.second);
    }

    return parts.join(QStringLiteral(" · "));
}

} // namespace FilePilot

