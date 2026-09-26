#pragma once

#include "core/duplicates/DuplicateTask.h"
#include "core/model/TaskState.h"
#include "core/scan/ScanService.h"
#include "core/tasks/ScanTask.h"

#include <QWidget>

#include <vector>

class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QResizeEvent;
class QSplitter;
class QStackedWidget;
class QTableView;
class QTabWidget;

namespace FilePilot {

class DuplicateErrorModel;
class DuplicateGroupModel;
class DuplicateItemModel;

class DuplicateFilesPage : public QWidget
{
    Q_OBJECT

public:
    explicit DuplicateFilesPage(QWidget *parent = nullptr);

protected:
    void resizeEvent(QResizeEvent *event) override;

public slots:
    void chooseDirectory();
    void startScan();
    void cancelScan();

private slots:
    void handleScanState(FilePilot::TaskState state);
    void handleScanProgress(qint64 scannedFileCount,
                            QString currentDirectory,
                            QString currentFile);
    void handleScanErrorBatch(FilePilot::ScanErrorBatch batch);
    void handleScanCompleted(FilePilot::ScanResult result);
    void handleScanFailure(QString message);
    void handleScanCancelled();

    void handleDuplicateState(FilePilot::TaskState state);
    void handleDuplicateProgress(FilePilot::DuplicateProgress progress);
    void handleDuplicateError(FilePilot::DuplicateError error);
    void handleDuplicateCompleted(FilePilot::DuplicateResult result);
    void handleDuplicateFailure(QString message);
    void handleDuplicateCancelled();

private:
    enum class ScanStage {
        Idle,
        Inventory,
        DuplicateHashing,
    };

    void buildUi();
    void resetResults(const QString &title, const QString &message);
    void startDuplicateHashing(const ScanResult &scanResult);
    void showDuplicateResult(const DuplicateResult &result);
    void showSelectedGroup();
    void updateSummary(const DuplicateSummary &summary);
    void updateControls();
    void updateResultsSplitterOrientation();
    void setStageText(const QString &stage, const QString &currentFile);
    void setStatusText(const QString &message, bool isError = false);
    void rebuildErrorModel();

    ScanTask scanTask_;
    DuplicateTask duplicateTask_;
    std::vector<ScanError> scanErrors_;
    std::vector<DuplicateError> duplicateErrors_;
    ScanStage stage_ = ScanStage::Idle;
    bool busy_ = false;
    bool cancelRequested_ = false;

    QLineEdit *directoryEdit_ = nullptr;
    QPushButton *chooseDirectoryButton_ = nullptr;
    QPushButton *scanButton_ = nullptr;
    QLabel *groupCountValueLabel_ = nullptr;
    QLabel *duplicateFileCountValueLabel_ = nullptr;
    QLabel *duplicateSizeValueLabel_ = nullptr;
    QLabel *wastedSizeValueLabel_ = nullptr;
    QLabel *stageLabel_ = nullptr;
    QLabel *currentFileLabel_ = nullptr;
    QProgressBar *progressBar_ = nullptr;
    QLabel *progressValueLabel_ = nullptr;
    QPushButton *cancelButton_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QStackedWidget *resultsStack_ = nullptr;
    QLabel *emptyStateLabel_ = nullptr;
    QTableView *groupTableView_ = nullptr;
    QTableView *itemTableView_ = nullptr;
    QTableView *errorTableView_ = nullptr;
    QSplitter *resultsSplitter_ = nullptr;
    QTabWidget *detailTabs_ = nullptr;
    DuplicateGroupModel *groupModel_ = nullptr;
    DuplicateItemModel *itemModel_ = nullptr;
    DuplicateErrorModel *errorModel_ = nullptr;
};

} // namespace FilePilot
