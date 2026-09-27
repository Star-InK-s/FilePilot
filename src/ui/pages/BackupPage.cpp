#include "ui/pages/BackupPage.h"

#include "app/Application.h"
#include "core/database/BackupHistoryTypes.h"

#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace FilePilot {

namespace {

ConflictPolicy policyFromIndex(const int index)
{
    switch (index) {
    case 0:
        return ConflictPolicy::Skip;
    case 1:
        return ConflictPolicy::Overwrite;
    default:
        return ConflictPolicy::AutoRename;
    }
}

QString statusText(const BackupExecutionStatus status)
{
    switch (status) {
    case BackupExecutionStatus::Succeeded:
        return QStringLiteral("成功");
    case BackupExecutionStatus::Skipped:
        return QStringLiteral("已跳过");
    case BackupExecutionStatus::Cancelled:
        return QStringLiteral("已取消");
    case BackupExecutionStatus::VerificationFailed:
        return QStringLiteral("验证失败");
    case BackupExecutionStatus::SourceChanged:
        return QStringLiteral("源已变化");
    case BackupExecutionStatus::DestinationConflict:
        return QStringLiteral("目标冲突");
    case BackupExecutionStatus::PublishFailed:
        return QStringLiteral("发布失败");
    case BackupExecutionStatus::CleanupFailed:
        return QStringLiteral("清理失败");
    case BackupExecutionStatus::Failed:
        break;
    }
    return QStringLiteral("失败");
}

} // namespace

BackupPage::BackupPage(Application &application, QWidget *parent)
    : QWidget(parent)
    , application_(application)
{
    setObjectName(QStringLiteral("pageBackup"));
    setWindowTitle(QStringLiteral("备份"));
    buildUi();
    updateControls();
}

void BackupPage::buildUi()
{
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(32, 28, 32, 28);
    rootLayout->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("备份"), this);
    QFont titleFont = title->font();
    titleFont.setPointSize(16);
    titleFont.setBold(true);
    title->setFont(titleFont);
    rootLayout->addWidget(title);

    auto *form = new QFormLayout();
    form->setSpacing(10);

    auto *sourceRow = new QHBoxLayout();
    sourceEdit_ = new QLineEdit(this);
    sourceEdit_->setObjectName(QStringLiteral("backupSourceEdit"));
    sourceEdit_->setPlaceholderText(QStringLiteral("选择要备份的文件或目录"));
    sourceRow->addWidget(sourceEdit_, 1);
    chooseSourceFileButton_ = new QPushButton(QStringLiteral("选择文件"), this);
    chooseSourceFileButton_->setObjectName(QStringLiteral("chooseBackupSourceFileButton"));
    sourceRow->addWidget(chooseSourceFileButton_);
    chooseSourceDirectoryButton_ = new QPushButton(QStringLiteral("选择目录"), this);
    chooseSourceDirectoryButton_->setObjectName(QStringLiteral("chooseBackupSourceDirectoryButton"));
    sourceRow->addWidget(chooseSourceDirectoryButton_);
    form->addRow(QStringLiteral("备份源"), sourceRow);

    auto *destinationRow = new QHBoxLayout();
    destinationEdit_ = new QLineEdit(this);
    destinationEdit_->setObjectName(QStringLiteral("backupDestinationEdit"));
    destinationEdit_->setPlaceholderText(QStringLiteral("选择备份目标根目录"));
    destinationRow->addWidget(destinationEdit_, 1);
    chooseDestinationButton_ = new QPushButton(QStringLiteral("选择目录"), this);
    chooseDestinationButton_->setObjectName(QStringLiteral("chooseBackupDestinationButton"));
    destinationRow->addWidget(chooseDestinationButton_);
    form->addRow(QStringLiteral("目标根目录"), destinationRow);

    conflictPolicyCombo_ = new QComboBox(this);
    conflictPolicyCombo_->setObjectName(QStringLiteral("backupConflictPolicyCombo"));
    conflictPolicyCombo_->addItems({
        QStringLiteral("跳过"),
        QStringLiteral("覆盖文件"),
        QStringLiteral("自动重命名"),
    });
    conflictPolicyCombo_->setCurrentIndex(2);
    form->addRow(QStringLiteral("冲突策略"), conflictPolicyCombo_);
    rootLayout->addLayout(form);

    auto *previewRow = new QHBoxLayout();
    previewButton_ = new QPushButton(QStringLiteral("生成预览"), this);
    previewButton_->setObjectName(QStringLiteral("buildBackupPreviewButton"));
    previewRow->addWidget(previewButton_);
    previewRow->addStretch(1);
    rootLayout->addLayout(previewRow);

    previewList_ = new QListWidget(this);
    previewList_->setObjectName(QStringLiteral("backupPreviewList"));
    rootLayout->addWidget(previewList_, 1);

    auto *actionRow = new QHBoxLayout();
    startButton_ = new QPushButton(QStringLiteral("开始备份"), this);
    startButton_->setObjectName(QStringLiteral("startBackupButton"));
    startButton_->setDefault(true);
    actionRow->addWidget(startButton_);
    cancelButton_ = new QPushButton(QStringLiteral("取消"), this);
    cancelButton_->setObjectName(QStringLiteral("cancelBackupButton"));
    actionRow->addWidget(cancelButton_);
    actionRow->addStretch(1);
    rootLayout->addLayout(actionRow);

    progressBar_ = new QProgressBar(this);
    progressBar_->setObjectName(QStringLiteral("backupProgressBar"));
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    rootLayout->addWidget(progressBar_);

    currentFileLabel_ = new QLabel(QStringLiteral("当前文件：无"), this);
    currentFileLabel_->setObjectName(QStringLiteral("backupCurrentFileLabel"));
    currentFileLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rootLayout->addWidget(currentFileLabel_);

    summaryLabel_ = new QLabel(QStringLiteral("尚未执行备份"), this);
    summaryLabel_->setObjectName(QStringLiteral("backupSummaryLabel"));
    summaryLabel_->setWordWrap(true);
    rootLayout->addWidget(summaryLabel_);

    errorLabel_ = new QLabel(this);
    errorLabel_->setObjectName(QStringLiteral("backupErrorLabel"));
    errorLabel_->setWordWrap(true);
    errorLabel_->setStyleSheet(QStringLiteral("color: #c42b1c;"));
    rootLayout->addWidget(errorLabel_);

    connect(chooseSourceFileButton_, &QPushButton::clicked, this, &BackupPage::chooseSourceFile);
    connect(chooseSourceDirectoryButton_, &QPushButton::clicked, this, &BackupPage::chooseSourceDirectory);
    connect(chooseDestinationButton_, &QPushButton::clicked, this, &BackupPage::chooseDestinationRoot);
    connect(previewButton_, &QPushButton::clicked, this, &BackupPage::buildPreview);
    connect(startButton_, &QPushButton::clicked, this, &BackupPage::startBackup);
    connect(cancelButton_, &QPushButton::clicked, this, &BackupPage::cancelBackup);
    connect(sourceEdit_, &QLineEdit::textChanged, this, [this](const QString &) {
        hasPreview_ = false;
        updateControls();
    });
    connect(destinationEdit_, &QLineEdit::textChanged, this, [this](const QString &) {
        hasPreview_ = false;
        updateControls();
    });
    connect(conflictPolicyCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        hasPreview_ = false;
        updateControls();
    });

    connect(&task_, &BackupTask::stateChanged, this, &BackupPage::handleState);
    connect(&task_, &BackupTask::progressChanged, this, &BackupPage::handleProgress);
    connect(&task_, &BackupTask::currentFileChanged, this, &BackupPage::handleCurrentFile);
    connect(&task_, &BackupTask::completed, this, &BackupPage::handleCompleted);
    connect(&task_, &BackupTask::failed, this, &BackupPage::handleFailure);
    connect(&task_, &BackupTask::cancelled, this, &BackupPage::handleCancelled);
}

void BackupPage::chooseSourceFile()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择备份源文件"));
    if (!path.isEmpty()) {
        sourceEdit_->setText(path);
    }
}

void BackupPage::chooseSourceDirectory()
{
    const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择备份源目录"));
    if (!path.isEmpty()) {
        sourceEdit_->setText(path);
    }
}

void BackupPage::chooseDestinationRoot()
{
    const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择备份目标根目录"));
    if (!path.isEmpty()) {
        destinationEdit_->setText(path);
    }
}

void BackupPage::buildPreview()
{
    previewList_->clear();
    errorLabel_->clear();
    QString error;
    if (!planBuilder_.build(
            sourceEdit_->text(),
            destinationEdit_->text(),
            policyFromIndex(conflictPolicyCombo_->currentIndex()),
            plan_,
            error)) {
        hasPreview_ = false;
        errorLabel_->setText(error);
        updateControls();
        return;
    }

    previewList_->addItem(QStringLiteral("目标：%1").arg(plan_.finalDestinationPath));
    for (const BackupPlanItem &item : plan_.items) {
        previewList_->addItem(QStringLiteral("%1  →  %2")
            .arg(item.relativePath, item.plannedDestinationPath));
    }
    for (const BackupUnsupportedEntry &entry : plan_.unsupportedEntries) {
        previewList_->addItem(QStringLiteral("不支持：%1（%2）")
            .arg(entry.relativePath, entry.reason));
    }
    hasPreview_ = true;
    summaryLabel_->setText(QStringLiteral("预览：%1 个文件，%2 个目录，%3 字节")
        .arg(plan_.expectedFileCount)
        .arg(plan_.expectedDirectoryCount)
        .arg(plan_.expectedBytes));
    updateControls();
}

void BackupPage::startBackup()
{
    if (!hasPreview_ || task_.isActive()) {
        return;
    }
    errorLabel_->clear();
    summaryLabel_->setText(QStringLiteral("正在准备备份…"));
    progressBar_->setValue(0);
    if (!task_.start(plan_)) {
        errorLabel_->setText(QStringLiteral("备份任务无法启动"));
        updateControls();
    }
}

void BackupPage::cancelBackup()
{
    task_.cancel();
}

void BackupPage::handleState(const TaskState state)
{
    Q_UNUSED(state);
    updateControls();
}

void BackupPage::handleProgress(
    const qint64 completed,
    const qint64 total,
    const QString currentFile,
    const QString phase)
{
    progressBar_->setMaximum(total > 0 ? static_cast<int>(total) : 1);
    progressBar_->setValue(static_cast<int>(completed));
    currentFileLabel_->setText(QStringLiteral("当前文件：%1（%2）").arg(currentFile, phase));
}

void BackupPage::handleCurrentFile(const QString currentFile)
{
    currentFileLabel_->setText(QStringLiteral("当前文件：%1").arg(currentFile));
}

void BackupPage::handleCompleted(const BackupExecutionResult result)
{
    QString historyNote;
    const BackupHistoryPersistenceResult persistence =
        application_.backupHistoryRepository().saveBackupResult(plan_, result);
    if (!persistence.persisted) {
        historyNote = QStringLiteral("；历史记录失败：%1").arg(persistence.errorMessage);
    }
    showResult(result, historyNote);
}

void BackupPage::handleFailure(const QString message)
{
    errorLabel_->setText(message);
}

void BackupPage::handleCancelled()
{
    summaryLabel_->setText(QStringLiteral("备份已取消"));
    updateControls();
}

void BackupPage::showResult(
    const BackupExecutionResult &result,
    const QString &historyNote)
{
    summaryLabel_->setText(
        QStringLiteral("结果：%1；已发布：%2；已验证：%3；源保留：%4；清理完成：%5%6")
            .arg(statusText(result.status))
            .arg(result.published ? QStringLiteral("是") : QStringLiteral("否"))
            .arg(result.verified ? QStringLiteral("是") : QStringLiteral("否"))
            .arg(result.sourcePreserved ? QStringLiteral("是") : QStringLiteral("否"))
            .arg(result.cleanupComplete ? QStringLiteral("是") : QStringLiteral("否"))
            .arg(historyNote));
    if (!result.errorMessage.isEmpty()) {
        errorLabel_->setText(result.errorMessage);
    }
    updateControls();
}

void BackupPage::updateControls()
{
    const bool active = task_.isActive();
    previewButton_->setEnabled(!active);
    startButton_->setEnabled(hasPreview_ && !active);
    cancelButton_->setEnabled(active);
    sourceEdit_->setEnabled(!active);
    destinationEdit_->setEnabled(!active);
    conflictPolicyCombo_->setEnabled(!active);
    chooseSourceFileButton_->setEnabled(!active);
    chooseSourceDirectoryButton_->setEnabled(!active);
    chooseDestinationButton_->setEnabled(!active);
}

} // namespace FilePilot
