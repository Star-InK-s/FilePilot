#include "app/MainWindow.h"

#include "app/Application.h"
#include "ui/pages/Pages.h"

#include <QAction>
#include <QCloseEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QVBoxLayout>

namespace FilePilot {

MainWindow::MainWindow(Application &application, QWidget *parent)
    : QMainWindow(parent)
    , application_(application)
{
    buildUi();
    restoreWindowState();

    application_.logger().log(LogLevel::Info,
                             QStringLiteral("MainWindow"),
                             QStringLiteral("Main window initialized"));
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("FilePilot"));
    setMinimumSize(960, 640);
    resize(1200, 800);

    addToolBar(Qt::TopToolBarArea, buildToolBar());

    auto *centralWidget = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(centralWidget);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto *contentLayout = new QHBoxLayout();
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    navigation_ = buildNavigation();
    pageStack_ = buildPages();

    contentLayout->addWidget(navigation_);
    contentLayout->addWidget(pageStack_, 1);
    rootLayout->addLayout(contentLayout, 1);
    rootLayout->addWidget(buildProgressPanel());

    setCentralWidget(centralWidget);
    statusBar()->setSizeGripEnabled(true);
    statusBar()->showMessage(QStringLiteral("就绪"));

    connect(navigation_,
            &QListWidget::currentRowChanged,
            pageStack_,
            &QStackedWidget::setCurrentIndex);
    connect(pageStack_,
            &QStackedWidget::currentChanged,
            this,
            [this](const int index) {
                if (index < 0 || index >= pageStack_->count()) {
                    return;
                }

                const QString pageName =
                    pageStack_->widget(index)->windowTitle().isEmpty()
                        ? navigation_->item(index)->text()
                        : pageStack_->widget(index)->windowTitle();
                statusBar()->showMessage(QStringLiteral("当前页面：%1").arg(pageName));
            });

    navigation_->setCurrentRow(0);
}

QToolBar *MainWindow::buildToolBar()
{
    auto *toolBar = new QToolBar(QStringLiteral("主工具栏"), this);
    toolBar->setObjectName(QStringLiteral("mainToolBar"));
    toolBar->setMovable(false);
    toolBar->setFloatable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    const struct {
        QString text;
        QStyle::StandardPixmap icon;
    } actions[] = {
        {QStringLiteral("选择目录"), QStyle::SP_DialogOpenButton},
        {QStringLiteral("扫描"), QStyle::SP_BrowserReload},
        {QStringLiteral("整理"), QStyle::SP_DialogApplyButton},
        {QStringLiteral("检测重复"), QStyle::SP_FileDialogContentsView},
        {QStringLiteral("开始备份"), QStyle::SP_DialogSaveButton},
    };

    for (const auto &entry : actions) {
        QAction *action =
            toolBar->addAction(style()->standardIcon(entry.icon), entry.text);
        action->setEnabled(false);
    }

    return toolBar;
}

QListWidget *MainWindow::buildNavigation()
{
    auto *navigation = new QListWidget(this);
    navigation->setObjectName(QStringLiteral("navigation"));
    navigation->setFixedWidth(188);
    navigation->setFocusPolicy(Qt::NoFocus);
    navigation->setIconSize(QSize(18, 18));
    navigation->setSpacing(2);

    const struct {
        QString text;
        QStyle::StandardPixmap icon;
    } entries[] = {
        {QStringLiteral("文件整理"), QStyle::SP_DirOpenIcon},
        {QStringLiteral("重复文件"), QStyle::SP_FileDialogDetailedView},
        {QStringLiteral("备份"), QStyle::SP_DriveHDIcon},
        {QStringLiteral("历史记录"), QStyle::SP_FileDialogContentsView},
        {QStringLiteral("设置"), QStyle::SP_FileDialogInfoView},
    };

    for (const auto &entry : entries) {
        auto *item = new QListWidgetItem(style()->standardIcon(entry.icon), entry.text);
        item->setSizeHint(QSize(item->sizeHint().width(), 42));
        navigation->addItem(item);
    }

    return navigation;
}

QStackedWidget *MainWindow::buildPages()
{
    auto *pageStack = new QStackedWidget(this);
    pageStack->setObjectName(QStringLiteral("pageStack"));

    pageStack->addWidget(new FileOrganizePage(pageStack));
    pageStack->addWidget(new DuplicateFilesPage(pageStack));
    pageStack->addWidget(new BackupPage(pageStack));
    pageStack->addWidget(new HistoryPage(pageStack));
    pageStack->addWidget(new SettingsPage(pageStack));

    return pageStack;
}

QWidget *MainWindow::buildProgressPanel()
{
    auto *panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("globalProgressPanel"));
    panel->setFrameShape(QFrame::StyledPanel);

    auto *layout = new QHBoxLayout(panel);
    layout->setContentsMargins(16, 10, 16, 10);
    layout->setSpacing(12);

    currentTaskLabel_ = new QLabel(QStringLiteral("当前任务：无"), panel);
    currentTaskLabel_->setObjectName(QStringLiteral("currentTaskLabel"));
    layout->addWidget(currentTaskLabel_);

    taskProgressBar_ = new QProgressBar(panel);
    taskProgressBar_->setObjectName(QStringLiteral("globalProgressBar"));
    taskProgressBar_->setRange(0, 100);
    taskProgressBar_->setValue(0);
    taskProgressBar_->setMinimumWidth(220);
    taskProgressBar_->setTextVisible(true);
    layout->addWidget(taskProgressBar_, 1);

    taskStateLabel_ = new QLabel(QStringLiteral("空闲"), panel);
    taskStateLabel_->setObjectName(QStringLiteral("taskStateLabel"));
    layout->addWidget(taskStateLabel_);

    cancelTaskButton_ = new QPushButton(
        style()->standardIcon(QStyle::SP_DialogCancelButton),
        QStringLiteral("取消任务"),
        panel);
    cancelTaskButton_->setObjectName(QStringLiteral("cancelTaskButton"));
    cancelTaskButton_->setEnabled(false);
    layout->addWidget(cancelTaskButton_);

    return panel;
}

void MainWindow::restoreWindowState()
{
    const QByteArray geometry = application_.settings().windowGeometry();
    const QByteArray state = application_.settings().windowState();

    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }
    if (!state.isEmpty()) {
        restoreState(state);
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    application_.settings().setWindowGeometry(saveGeometry());
    application_.settings().setWindowState(saveState());
    application_.settings().sync();
    application_.logger().log(LogLevel::Info,
                             QStringLiteral("MainWindow"),
                             QStringLiteral("Main window closed"));

    QMainWindow::closeEvent(event);
}

} // namespace FilePilot
