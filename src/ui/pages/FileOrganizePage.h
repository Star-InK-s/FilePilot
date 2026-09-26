#pragma once

#include "core/model/TaskState.h"
#include "core/execution/ExecutionTypes.h"
#include "core/execution/OrganizeExecutionTask.h"
#include "core/organize/OrganizePlan.h"
#include "core/tasks/ScanTask.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QProgressBar;
class QTableView;

namespace FilePilot {

class Application;
class FileTableModel;
class OrganizePreviewModel;
class ExecutionResultModel;

class FileOrganizePage : public QWidget
{
    Q_OBJECT

public:
    explicit FileOrganizePage(Application &application, QWidget *parent = nullptr);

public slots:
    void chooseDirectory();
    void startScan();
    void cancelScan();
    void chooseTargetRoot();
    void generatePreview();
    void confirmPlan();
    void cancelPlan();
    void startExecution();
    void cancelExecution();

signals:
    void taskStateChanged(FilePilot::TaskState state);
    void taskProgressChanged(qint64 scannedFileCount,
                             QString currentDirectory,
                             QString currentFile);
    void taskErrorCountChanged(qint64 errorCount);

private slots:
    void handleTaskState(FilePilot::TaskState state);
    void handleProgress(qint64 scannedFileCount,
                        QString currentDirectory,
                        QString currentFile);
    void handleErrorBatch(FilePilot::ScanErrorBatch batch);
    void handleCompleted(FilePilot::ScanResult result);
    void handleFailed(QString message);
    void handleCancelled();
    void handleExecutionState(TaskState state);
    void handleExecutionProgress(qint64 completed, qint64 total, QString currentFile);
    void handleExecutionItemProgress(ExecutionProgressUpdate progress);
    void handleExecutionResult(ExecutionResult result);
    void handleExecutionFailure(QString message);
    void handleExecutionCancelled();

private:
    void buildUi();
    void updateControls(TaskState state);
    void updateSummary(qint64 fileCount,
                       qint64 totalSizeBytes,
                       qint64 errorCount,
                       const QHash<QString, qint64> &extensionCounts,
                       const QHash<QString, qint64> &categoryCounts);
    static QString countSummary(const QHash<QString, qint64> &counts);
    void clearPreview();
    void updatePreviewSummary();
    void invalidatePlan(const QString &reason);
    void resetExecutionPresentation();
    bool planMatchesCurrentInputs() const;

    Application &application_;
    ScanTask scanTask_;
    OrganizeExecutionTask executionTask_;
    FileTableModel *fileModel_ = nullptr;
    QLineEdit *directoryEdit_ = nullptr;
    QPushButton *chooseDirectoryButton_ = nullptr;
    QPushButton *scanButton_ = nullptr;
    QLabel *fileCountValueLabel_ = nullptr;
    QLabel *totalSizeValueLabel_ = nullptr;
    QLabel *errorCountValueLabel_ = nullptr;
    QLabel *extensionStatsLabel_ = nullptr;
    QLabel *categoryStatsLabel_ = nullptr;
    QLabel *scanStatusLabel_ = nullptr;
    QTableView *fileTableView_ = nullptr;
    QLabel *previewTotalLabel_ = nullptr;
    QLabel *previewPlannedLabel_ = nullptr;
    QLabel *previewInvalidLabel_ = nullptr;
    QLabel *previewNoOpLabel_ = nullptr;
    QLabel *previewCategoryStatsLabel_ = nullptr;
    QLabel *previewStatusLabel_ = nullptr;
    QLineEdit *targetRootEdit_ = nullptr;
    QPushButton *chooseTargetRootButton_ = nullptr;
    QPushButton *generatePreviewButton_ = nullptr;
    QPushButton *confirmPlanButton_ = nullptr;
    QPushButton *cancelPlanButton_ = nullptr;
    QPushButton *executePlanButton_ = nullptr;
    QPushButton *cancelExecutionButton_ = nullptr;
    QTableView *previewTableView_ = nullptr;
    QLabel *executionCurrentFileLabel_ = nullptr;
    QLabel *executionSummaryLabel_ = nullptr;
    QProgressBar *executionProgressBar_ = nullptr;
    QTableView *executionResultTableView_ = nullptr;
    OrganizePreviewModel *previewModel_ = nullptr;
    ExecutionResultModel *executionResultModel_ = nullptr;
    ScanResult lastScanResult_;
    OrganizePlan currentPlan_;
    bool hasScanResult_ = false;
    bool planConfirmed_ = false;
    bool planLocked_ = false;
    TaskState executionState_ = TaskState::Idle;
    quint64 planGeneration_ = 0;
    quint64 scanGeneration_ = 0;
    QString currentScanSourceRoot_;
    qint64 errorCount_ = 0;
    QString currentRoot_;
};

} // namespace FilePilot
