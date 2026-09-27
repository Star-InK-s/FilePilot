#pragma once

#include "core/backup/BackupPlanBuilder.h"
#include "core/backup/BackupPlanTypes.h"
#include "core/backup/BackupTask.h"
#include "core/model/ConflictPolicy.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;

namespace FilePilot {

class Application;

class BackupPage : public QWidget
{
    Q_OBJECT

public:
    explicit BackupPage(Application &application, QWidget *parent = nullptr);

public slots:
    void chooseSourceFile();
    void chooseSourceDirectory();
    void chooseDestinationRoot();
    void buildPreview();
    void startBackup();
    void cancelBackup();

private slots:
    void handleState(FilePilot::TaskState state);
    void handleProgress(qint64 completed,
                        qint64 total,
                        QString currentFile,
                        QString phase);
    void handleCurrentFile(QString currentFile);
    void handleCompleted(FilePilot::BackupExecutionResult result);
    void handleFailure(QString message);
    void handleCancelled();

private:
    void buildUi();
    void updateControls();
    void showResult(const BackupExecutionResult &result, const QString &historyNote);

    Application &application_;
    BackupPlanBuilder planBuilder_;
    BackupTask task_;
    BackupPlan plan_;
    bool hasPreview_ = false;

    QLineEdit *sourceEdit_ = nullptr;
    QPushButton *chooseSourceFileButton_ = nullptr;
    QPushButton *chooseSourceDirectoryButton_ = nullptr;
    QLineEdit *destinationEdit_ = nullptr;
    QPushButton *chooseDestinationButton_ = nullptr;
    QComboBox *conflictPolicyCombo_ = nullptr;
    QPushButton *previewButton_ = nullptr;
    QListWidget *previewList_ = nullptr;
    QPushButton *startButton_ = nullptr;
    QPushButton *cancelButton_ = nullptr;
    QProgressBar *progressBar_ = nullptr;
    QLabel *currentFileLabel_ = nullptr;
    QLabel *summaryLabel_ = nullptr;
    QLabel *errorLabel_ = nullptr;
};

} // namespace FilePilot
