#include "app/MainWindow.h"

#include "app/Application.h"
#include "ui/pages/BackupPage.h"
#include "ui/pages/DuplicateFilesPage.h"
#include "ui/pages/FileOrganizePage.h"
#include "ui/pages/Pages.h"
#include "ui/presenters/DuplicateTheme.h"
#include "ui/theme/QtThemeApplier.h"
#include "platform/windows/WindowsThemeDetector.h"

#include <QAction>
#include <QCloseEvent>
#include <QEvent>
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
    if (auto *toolBar = findChild<QToolBar *>(QStringLiteral("mainToolBar"))) {
        toolBar->hide();
    }

    application_.logger().log(LogLevel::Info,
                             QStringLiteral("MainWindow"),
                             QStringLiteral("Main window initialized"));
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("FilePilot"));
    setMinimumSize(960, 640);
    resize(1280, 820);

    auto *toolBar = buildToolBar();
    addToolBar(Qt::TopToolBarArea, toolBar);
    toolBar->hide();

    auto *centralWidget = new QWidget(this);
    centralWidget->setObjectName(QStringLiteral("centralWidget"));
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
                updatePageCommands(index);
            });

    connect(chooseDirectoryAction_,
            &QAction::triggered,
            this,
            [this] {
                if (pageStack_->currentWidget() == duplicatePage_) {
                    duplicatePage_->chooseDirectory();
                } else {
                    organizePage_->chooseDirectory();
                }
            });
    connect(scanAction_,
            &QAction::triggered,
            this,
            [this] {
                if (pageStack_->currentWidget() == duplicatePage_) {
                    duplicatePage_->startScan();
                } else {
                    organizePage_->startScan();
                }
            });
    connect(cancelTaskButton_,
            &QPushButton::clicked,
            organizePage_,
            &FileOrganizePage::cancelScan);
    connect(organizePage_,
            &FileOrganizePage::taskStateChanged,
            this,
            &MainWindow::updateTaskState);
    connect(organizePage_,
            &FileOrganizePage::taskProgressChanged,
            this,
            &MainWindow::updateTaskProgress);
    connect(organizePage_,
            &FileOrganizePage::taskErrorCountChanged,
            this,
            &MainWindow::updateTaskErrorCount);

    themeApplier_ = new QtThemeApplier(this);
    themeDetector_ = new WindowsThemeDetector(this);
    connect(themeDetector_,
            &WindowsThemeDetector::themeChanged,
            this,
            &MainWindow::applyTheme);
    themeDetector_->start();
    refreshFluentIcons();
    updateTaskState(TaskState::Idle);
    navigation_->setCurrentRow(0);
}

QToolBar *MainWindow::buildToolBar()
{
    auto *toolBar = new QToolBar(QStringLiteral("主工具栏"), this);
    toolBar->setObjectName(QStringLiteral("mainToolBar"));
    toolBar->setMovable(false);
    toolBar->setFloatable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolBar->setIconSize(QSize(18, 18));

    chooseDirectoryAction_ = toolBar->addAction(QStringLiteral("选择目录"));
    chooseDirectoryAction_->setObjectName(QStringLiteral("chooseDirectoryAction"));

    scanAction_ = toolBar->addAction(QStringLiteral("扫描"));
    scanAction_->setObjectName(QStringLiteral("scanAction"));

    organizeAction_ = toolBar->addAction(QStringLiteral("生成整理计划"));
    organizeAction_->setObjectName(QStringLiteral("organizeAction"));
    organizeAction_->setEnabled(false);

    duplicateAction_ = toolBar->addAction(QStringLiteral("检测重复"));
    duplicateAction_->setObjectName(QStringLiteral("duplicateAction"));
    duplicateAction_->setEnabled(false);

    backupAction_ = toolBar->addAction(QStringLiteral("开始备份"));
    backupAction_->setObjectName(QStringLiteral("backupAction"));
    backupAction_->setEnabled(false);

    return toolBar;
}

QListWidget *MainWindow::buildNavigation()
{
    auto *navigation = new QListWidget(this);
    navigation->setObjectName(QStringLiteral("navigation"));
    navigation->setFixedWidth(204);
    navigation->setFocusPolicy(Qt::StrongFocus);
    navigation->setIconSize(QSize(20, 20));
    navigation->setSpacing(0);
    navigation->setUniformItemSizes(true);
    navigation->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    const struct {
        QString text;
        unsigned short glyph;
        QStyle::StandardPixmap fallback;
    } entries[] = {
        {QStringLiteral("文件整理"), 0xE838, QStyle::SP_DirOpenIcon},
        {QStringLiteral("重复文件"), 0xE8C8, QStyle::SP_FileDialogDetailedView},
        {QStringLiteral("备份"), 0xE74E, QStyle::SP_DriveHDIcon},
        {QStringLiteral("历史记录"), 0xE81C, QStyle::SP_FileDialogContentsView},
        {QStringLiteral("设置"), 0xE713, QStyle::SP_FileDialogInfoView},
    };

    for (const auto &entry : entries) {
        auto *item = new QListWidgetItem(
            style()->standardIcon(entry.fallback), entry.text);
        item->setData(Qt::UserRole, entry.glyph);
        item->setData(Qt::UserRole + 1, static_cast<int>(entry.fallback));
        item->setSizeHint(QSize(item->sizeHint().width(), 40));
        navigation->addItem(item);
    }

    return navigation;
}

QStackedWidget *MainWindow::buildPages()
{
    auto *pageStack = new QStackedWidget(this);
    pageStack->setObjectName(QStringLiteral("pageStack"));

    organizePage_ = new FileOrganizePage(application_, pageStack);
    duplicatePage_ = new DuplicateFilesPage(pageStack);
    pageStack->addWidget(organizePage_);
    pageStack->addWidget(duplicatePage_);
    pageStack->addWidget(new BackupPage(application_, pageStack));
    pageStack->addWidget(
        new HistoryPage(application_.historyRepository(), pageStack));
    pageStack->addWidget(new SettingsPage(pageStack));

    return pageStack;
}

QWidget *MainWindow::buildProgressPanel()
{
    auto *panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("globalProgressPanel"));
    panel->setFrameShape(QFrame::NoFrame);

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
    taskProgressBar_->setTextVisible(false);
    layout->addWidget(taskProgressBar_, 1);

    taskStateLabel_ = new QLabel(QStringLiteral("空闲"), panel);
    taskStateLabel_->setObjectName(QStringLiteral("taskStateLabel"));
    layout->addWidget(taskStateLabel_);

    cancelTaskButton_ = new QPushButton(QStringLiteral("取消任务"), panel);
    cancelTaskButton_->setObjectName(QStringLiteral("cancelTaskButton"));
    cancelTaskButton_->setEnabled(false);
    layout->addWidget(cancelTaskButton_);

    panel->hide();
    return panel;
}

void MainWindow::applyTheme(const ThemeSnapshot &snapshot)
{
    if (applyingTheme_ || themeApplier_ == nullptr) {
        return;
    }

    applyingTheme_ = true;
    const ThemePalette palette = ThemePalette::fromSnapshot(snapshot);
    if (themeApplier_->apply(palette)) {
        refreshFluentIcons();
    }
    applyingTheme_ = false;
}

void MainWindow::updatePageCommands(const int index)
{
    const bool organizePage = index == 0;
    const bool duplicatePage = index == 1;

    chooseDirectoryAction_->setVisible(organizePage || duplicatePage);
    scanAction_->setVisible(organizePage || duplicatePage);
    organizeAction_->setVisible(organizePage);
    duplicateAction_->setVisible(false);
    backupAction_->setVisible(false);
}

void MainWindow::refreshFluentIcons()
{
    const auto setActionIcon =
        [this](QAction *action, const unsigned short glyph, const QStyle::StandardPixmap fallback) {
            QIcon icon = windowsGlyphIcon(this, glyph, 18);
            if (icon.isNull()) {
                icon = style()->standardIcon(fallback);
            }
            action->setIcon(icon);
        };

    setActionIcon(chooseDirectoryAction_, 0xE8DA, QStyle::SP_DirOpenIcon);
    setActionIcon(scanAction_, 0xE721, QStyle::SP_BrowserReload);
    setActionIcon(organizeAction_, 0xE838, QStyle::SP_DialogApplyButton);
    setActionIcon(duplicateAction_, 0xE8C8, QStyle::SP_FileDialogContentsView);
    setActionIcon(backupAction_, 0xE74E, QStyle::SP_DialogSaveButton);

    for (int row = 0; row < navigation_->count(); ++row) {
        QListWidgetItem *item = navigation_->item(row);
        QIcon icon = windowsGlyphIcon(
            this,
            static_cast<unsigned short>(item->data(Qt::UserRole).toUInt()),
            20);
        if (icon.isNull()) {
            icon = style()->standardIcon(static_cast<QStyle::StandardPixmap>(
                item->data(Qt::UserRole + 1).toInt()));
        }
        item->setIcon(icon);
    }
}

void MainWindow::updateTaskState(const TaskState state)
{
    const bool active = state == TaskState::Preparing
        || state == TaskState::Running
        || state == TaskState::Cancelling;

    taskStateLabel_->setText(taskStateName(state));
    cancelTaskButton_->setEnabled(
        state == TaskState::Preparing || state == TaskState::Running);
    chooseDirectoryAction_->setEnabled(!active);
    scanAction_->setEnabled(!active);
    if (auto *panel = findChild<QFrame *>(QStringLiteral("globalProgressPanel"))) {
        panel->setVisible(active);
    }

    if (active) {
        currentTaskLabel_->setText(QStringLiteral("当前任务：扫描文件"));
        taskProgressBar_->setRange(0, 0);
        return;
    }

    currentTaskLabel_->setText(QStringLiteral("当前任务：无"));
    taskProgressBar_->setRange(0, 100);
    taskProgressBar_->setValue(state == TaskState::Completed ? 100 : 0);
}

void MainWindow::updateTaskProgress(const qint64 scannedFileCount,
                                    const QString &currentDirectory,
                                    const QString &currentFile)
{
    Q_UNUSED(currentFile)
    currentTaskLabel_->setText(
        QStringLiteral("当前任务：扫描文件（%1）").arg(scannedFileCount));
    if (!currentDirectory.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("当前目录：%1").arg(currentDirectory));
    }
}

void MainWindow::updateTaskErrorCount(const qint64 errorCount)
{
    if (errorCount > 0) {
        statusBar()->showMessage(QStringLiteral("扫描发现 %1 个错误").arg(errorCount));
    }
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

void MainWindow::changeEvent(QEvent *event)
{
    switch (event->type()) {
    case QEvent::ApplicationPaletteChange:
    case QEvent::PaletteChange:
    case QEvent::StyleChange:
    case QEvent::ThemeChange:
    case QEvent::FontChange:
    case QEvent::ScreenChangeInternal:
        refreshFluentIcons();
        break;
    default:
        break;
    }
    QMainWindow::changeEvent(event);
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
