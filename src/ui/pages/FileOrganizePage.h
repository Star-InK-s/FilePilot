#pragma once

#include "core/model/TaskState.h"
#include "core/tasks/ScanTask.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableView;

namespace FilePilot {

class Application;
class FileTableModel;

class FileOrganizePage : public QWidget
{
    Q_OBJECT

public:
    explicit FileOrganizePage(Application &application, QWidget *parent = nullptr);

public slots:
    void chooseDirectory();
    void startScan();
    void cancelScan();

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
    void handleError(FilePilot::ScanError error);
    void handleCompleted(FilePilot::ScanResult result);
    void handleFailed(QString message);
    void handleCancelled();

private:
    void buildUi();
    void updateControls(TaskState state);
    void updateSummary(qint64 fileCount,
                       qint64 totalSizeBytes,
                       qint64 errorCount,
                       const QHash<QString, qint64> &extensionCounts);
    static QString extensionSummary(const QHash<QString, qint64> &extensionCounts);

    Application &application_;
    ScanTask scanTask_;
    FileTableModel *fileModel_ = nullptr;
    QLineEdit *directoryEdit_ = nullptr;
    QPushButton *chooseDirectoryButton_ = nullptr;
    QPushButton *scanButton_ = nullptr;
    QLabel *fileCountValueLabel_ = nullptr;
    QLabel *totalSizeValueLabel_ = nullptr;
    QLabel *errorCountValueLabel_ = nullptr;
    QLabel *extensionStatsLabel_ = nullptr;
    QLabel *scanStatusLabel_ = nullptr;
    QTableView *fileTableView_ = nullptr;
    qint64 errorCount_ = 0;
    QString currentRoot_;
};

} // namespace FilePilot
