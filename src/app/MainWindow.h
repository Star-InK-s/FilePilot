#pragma once

#include <QMainWindow>

class QAction;
class QCloseEvent;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QStackedWidget;

namespace FilePilot {

class Application;

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

    Application &application_;
    QListWidget *navigation_ = nullptr;
    QStackedWidget *pageStack_ = nullptr;
    QLabel *currentTaskLabel_ = nullptr;
    QLabel *taskStateLabel_ = nullptr;
    QProgressBar *taskProgressBar_ = nullptr;
    QPushButton *cancelTaskButton_ = nullptr;
};

} // namespace FilePilot
