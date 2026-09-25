#pragma once

#include "core/model/TaskState.h"

#include <QMainWindow>

class QAction;
class QCloseEvent;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QToolBar;

namespace FilePilot {

class Application;
class FileOrganizePage;

class MainWindow : public QMainWindow
{
public:
    explicit MainWindow(Application &application, QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    QToolBar *buildToolBar();
    QListWidget *buildNavigation();
    QStackedWidget *buildPages();
    QWidget *buildProgressPanel();
    void restoreWindowState();
    void updateTaskState(TaskState state);
    void updateTaskProgress(qint64 scannedFileCount,
                            const QString &currentDirectory,
                            const QString &currentFile);
    void updateTaskErrorCount(qint64 errorCount);

    Application &application_;
    QListWidget *navigation_ = nullptr;
    QStackedWidget *pageStack_ = nullptr;
    FileOrganizePage *organizePage_ = nullptr;
    QAction *chooseDirectoryAction_ = nullptr;
    QAction *scanAction_ = nullptr;
    QLabel *currentTaskLabel_ = nullptr;
    QLabel *taskStateLabel_ = nullptr;
    QProgressBar *taskProgressBar_ = nullptr;
    QPushButton *cancelTaskButton_ = nullptr;
};

} // namespace FilePilot
