#pragma once

#include "core/model/TaskState.h"
#include "ui/theme/ThemeSnapshot.h"

#include <QMainWindow>

class QAction;
class QCloseEvent;
class QEvent;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QToolBar;

namespace FilePilot {

class Application;
class DuplicateFilesPage;
class FileOrganizePage;
class QtThemeApplier;
class WindowsThemeDetector;

class MainWindow : public QMainWindow
{
public:
    explicit MainWindow(Application &application, QWidget *parent = nullptr);

protected:
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    QToolBar *buildToolBar();
    QListWidget *buildNavigation();
    QStackedWidget *buildPages();
    QWidget *buildProgressPanel();
    void restoreWindowState();
    void applyTheme(const ThemeSnapshot &snapshot);
    void updatePageCommands(int index);
    void refreshFluentIcons();
    void updateTaskState(TaskState state);
    void updateTaskProgress(qint64 scannedFileCount,
                            const QString &currentDirectory,
                            const QString &currentFile);
    void updateTaskErrorCount(qint64 errorCount);

    Application &application_;
    WindowsThemeDetector *themeDetector_ = nullptr;
    QtThemeApplier *themeApplier_ = nullptr;
    bool applyingTheme_ = false;
    QListWidget *navigation_ = nullptr;
    QStackedWidget *pageStack_ = nullptr;
    FileOrganizePage *organizePage_ = nullptr;
    DuplicateFilesPage *duplicatePage_ = nullptr;
    QAction *chooseDirectoryAction_ = nullptr;
    QAction *scanAction_ = nullptr;
    QAction *organizeAction_ = nullptr;
    QAction *duplicateAction_ = nullptr;
    QAction *backupAction_ = nullptr;
    QLabel *currentTaskLabel_ = nullptr;
    QLabel *taskStateLabel_ = nullptr;
    QProgressBar *taskProgressBar_ = nullptr;
    QPushButton *cancelTaskButton_ = nullptr;
};

} // namespace FilePilot
