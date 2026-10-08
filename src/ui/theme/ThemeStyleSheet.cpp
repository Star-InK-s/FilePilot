#include "ui/theme/ThemeStyleSheet.h"

#include "ui/theme/ThemePalette.h"

#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QWidget>
#include <QtMath>

namespace FilePilot {

QString fluentStyleSheet(const ThemePalette &colors)
{
    const QString borderWidth = colors.highContrast()
        ? QStringLiteral("2px")
        : QStringLiteral("1px");
    const QString focusWidth = QStringLiteral("2px");
    const QString radius = colors.highContrast()
        ? QStringLiteral("2px")
        : QStringLiteral("4px");
    const QString progressRadius = colors.highContrast()
        ? QStringLiteral("0px")
        : QStringLiteral("2px");

    return QStringLiteral(R"(

        QWidget {
            color: %2;
            font-family: "Segoe UI Variable Text", "Segoe UI", "Microsoft YaHei UI", sans-serif;
            font-size: 14px;
        }
        QMainWindow,
        QWidget#centralWidget,
        QWidget#pageOrganize,
        QWidget#pageDuplicates,
        QWidget#pageBackup,
        QWidget#pageHistory,
        QWidget#pageSettings,
        QStackedWidget#pageStack,
        QDialog {
            background: %1;
            color: %2;
        }
        QLabel {
            background: transparent;
            color: %2;
        }
        QLabel#pageTitleLabel {
            font-size: 20px;
            font-weight: 600;
        }
        QLabel#pageSubtitleLabel,
        QLabel#scanStatusLabel {
            color: %5;
        }
        QLabel#pageSubtitleLabel {
            font-size: 13px;
        }
        QLabel[errorState="true"],
        QLabel#backupErrorLabel {
            color: %13;
        }
        QLabel:disabled {
            color: %11;
        }
        QToolTip {
            background: %3;
            color: %2;
            border: %8 solid %4;
            padding: 6px;
        }

        QToolBar#mainToolBar {
            background: %1;
            border: 0;
            border-bottom: %8 solid %4;
            spacing: 4px;
            padding: 6px 10px;
        }
        QToolBar#mainToolBar QToolButton {
            background: transparent;
            border: %8 solid transparent;
            border-radius: %9;
            color: %2;
            min-height: 30px;
            padding: 3px 9px;
        }
        QToolBar#mainToolBar QToolButton:hover {
            background: %6;
        }
        QToolBar#mainToolBar QToolButton:pressed {
            background: %7;
        }
        QToolBar#mainToolBar QToolButton:disabled {
            color: %11;
        }

        QListWidget#navigation {
            background: %1;
            color: %2;
            border: 0;
            border-right: %8 solid %4;
            outline: 0;
            padding: 8px;
        }
        QListWidget#navigation::item {
            min-height: 36px;
            margin: 1px 0;
            padding: 0 10px 0 9px;
            border: 0;
            border-left: 3px solid transparent;
            border-radius: %9;
        }
        QListWidget#navigation::item:hover {
            background: %6;
        }
        QListWidget#navigation::item:selected {
            background: %10;
            border-left: 3px solid %22;
            color: %21;
        }
        QListWidget#navigation::item:focus {
            outline: %20 solid %12;
            outline-offset: -2px;
        }

        QFrame#globalProgressPanel,
        QFrame#organizePreviewPanel {
            background: %3;
            border: 0;
            border-top: %8 solid %4;
        }
        QFrame#organizePreviewPanel {
            border-top: 0;
            border: %8 solid %4;
            border-radius: %9;
        }
        QFrame#duplicateSummaryPanel {
            background: transparent;
            border: 0;
            border-bottom: %8 solid %4;
        }
        QFrame#duplicateSummaryDivider {
            background: %4;
            border: 0;
            max-width: 1px;
            min-width: 1px;
        }
        QFrame#duplicateProgressPanel {
            background: transparent;
            border: 0;
            border-bottom: %8 solid %4;
        }

        QLabel#duplicateTitle {
            color: %2;
            font-size: 28px;
            font-weight: 600;
        }
        QLabel#duplicateSubtitle,
        QLabel#duplicateStatusLabel,
        QLabel#duplicateProgressValue {
            color: %5;
            font-size: 12px;
        }
        QLabel#duplicateGroupTitle,
        QLabel#duplicateDetailTitle {
            color: %2;
            font-size: 14px;
            font-weight: 600;
        }
        QLabel[duplicateMetricLabel="true"] {
            color: %5;
            font-size: 12px;
        }
        QLabel#duplicateGroupCountValue,
        QLabel#duplicateFileCountValue,
        QLabel#duplicateSizeValue,
        QLabel#duplicateWastedSizeValue {
            color: %2;
            font-size: 20px;
            font-weight: 600;
        }
        QLabel#duplicateStageLabel,
        QLabel#duplicateCurrentFileLabel {
            color: %2;
            font-size: 12px;
        }
        QLabel#duplicateEmptyStateLabel {
            color: %5;
            font-size: 14px;
        }

        QLineEdit {
            background: %6;
            color: %2;
            border: %8 solid %4;
            border-radius: %9;
            padding: 5px 8px;
            selection-background-color: %10;
            selection-color: %21;
        }
        QComboBox,
        QAbstractSpinBox,
        QTextEdit,
        QPlainTextEdit {
            background: %6;
            color: %2;
            border: %8 solid %4;
            border-radius: %9;
            padding: 5px 8px;
            selection-background-color: %10;
            selection-color: %21;
        }
        QLineEdit#searchEdit {
            max-width: 360px;
        }
        QLineEdit:focus {
            border: %20 solid %12;
            padding: 4px 7px;
        }
        QComboBox:focus,
        QAbstractSpinBox:focus,
        QTextEdit:focus,
        QPlainTextEdit:focus {
            border: %20 solid %12;
            padding: 4px 7px;
        }
        QLineEdit:disabled,
        QComboBox:disabled,
        QAbstractSpinBox:disabled,
        QTextEdit:disabled,
        QPlainTextEdit:disabled {
            background: %7;
            color: %11;
            border-color: %4;
        }
        QLineEdit:read-only,
        QTextEdit:read-only,
        QPlainTextEdit:read-only {
            background: %3;
        }

        QPushButton {
            background: %23;
            color: %14;
            border: %8 solid %22;
            border-radius: %9;
            min-height: 30px;
            padding: 3px 12px;
            font-weight: 600;
        }
        QPushButton:hover {
            background: %24;
            color: %18;
            border-color: %24;
        }
        QPushButton:pressed {
            background: %25;
            color: %19;
            border-color: %25;
        }
        QPushButton:focus {
            border: %20 solid %12;
            padding: 2px 11px;
        }
        QPushButton:disabled {
            background: %7;
            color: %11;
            border-color: %4;
        }
        QPushButton#deleteFilesButton {
            background: transparent;
            color: %13;
            border-color: %13;
        }
        QPushButton#deleteFilesButton:hover {
            background: %6;
            color: %13;
            border-color: %13;
        }
        QPushButton#deleteFilesButton:pressed {
            background: %7;
            color: %13;
            border-color: %13;
        }
        QPushButton#deleteFilesButton:disabled {
            background: %7;
            color: %11;
            border-color: %4;
        }
        QPushButton#startDuplicateScanButton {
            background: %23;
            color: %14;
            border-color: %23;
        }
        QPushButton#startDuplicateScanButton:hover {
            background: %24;
            color: %18;
            border-color: %24;
        }
        QPushButton#startDuplicateScanButton:pressed {
            background: %25;
            color: %19;
            border-color: %25;
        }
        QPushButton#startDuplicateScanButton:disabled {
            background: %7;
            color: %11;
            border-color: %4;
        }

        QProgressBar {
            background: %7;
            border: 0;
            border-radius: %17;
            min-height: 4px;
            max-height: 4px;
        }
        QProgressBar::chunk {
            background: %23;
            border-radius: %17;
        }

        QGroupBox {
            background: %3;
            color: %2;
            border: %8 solid %4;
            border-radius: %9;
            margin-top: 10px;
            padding-top: 8px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 4px;
            color: %5;
        }

        QAbstractItemView::viewport {
            background: %3;
        }
        QTableView {
            background: %3;
            alternate-background-color: %1;
            color: %2;
            border: %8 solid %4;
            border-radius: %9;
            gridline-color: transparent;
            selection-background-color: %10;
            selection-color: %21;
            outline: 0;
        }
        QListView,
        QTreeView {
            background: %3;
            alternate-background-color: %1;
            color: %2;
            border: %8 solid %4;
            border-radius: %9;
            gridline-color: transparent;
            selection-background-color: %10;
            selection-color: %21;
            outline: 0;
        }
        QTableView::item {
            padding: 6px 8px;
            border: 0;
        }
        QListView::item,
        QTreeView::item {
            padding: 6px 8px;
            border: 0;
        }
        QTableView::item:hover,
        QListView::item:hover,
        QTreeView::item:hover {
            background: %6;
        }
        QTableView::item:selected {
            background: %10;
            color: %21;
        }
        QListView::item:selected,
        QTreeView::item:selected {
            background: %10;
            color: %21;
        }
        QHeaderView::section {
            background: %1;
            color: %5;
            border: 0;
            border-bottom: %8 solid %4;
            padding: 7px 8px;
        }
        QHeaderView::section:hover {
            background: %6;
            color: %2;
        }
        QTableCornerButton::section {
            background: %1;
            border: 0;
            border-bottom: %8 solid %4;
        }

        QTabWidget::pane {
            background: %3;
            border: %8 solid %4;
            border-radius: %9;
            top: -1px;
        }
        QTabBar::tab {
            background: transparent;
            color: %5;
            border: 0;
            border-bottom: 2px solid transparent;
            padding: 8px 10px;
            min-width: 76px;
        }
        QTabBar::tab:hover {
            color: %2;
            background: %6;
        }
        QTabBar::tab:selected {
            color: %2;
            border-bottom: 2px solid %22;
        }
        QTabBar::tab:disabled {
            color: %11;
        }

        QSplitter::handle {
            background: %4;
            border: 0;
        }
        QSplitter::handle:horizontal {
            width: 1px;
            margin: 6px 0;
        }
        QSplitter::handle:vertical {
            height: 1px;
            margin: 0 6px;
        }

        QScrollBar:vertical {
            background: transparent;
            width: 12px;
            margin: 2px;
        }
        QScrollBar::handle:vertical {
            background: %5;
            min-height: 28px;
            border-radius: 5px;
        }
        QScrollBar::handle:vertical:hover {
            background: %15;
        }
        QScrollBar::handle:vertical:pressed {
            background: %16;
        }
        QScrollBar:horizontal {
            background: transparent;
            height: 12px;
            margin: 2px;
        }
        QScrollBar::handle:horizontal {
            background: %5;
            min-width: 28px;
            border-radius: 5px;
        }
        QScrollBar::handle:horizontal:hover {
            background: %15;
        }
        QScrollBar::handle:horizontal:pressed {
            background: %16;
        }
        QScrollBar::add-line,
        QScrollBar::sub-line {
            width: 0;
            height: 0;
        }
        QScrollBar::add-page,
        QScrollBar::sub-page {
            background: transparent;
        }

        QMenu {
            background: %3;
            color: %2;
            border: %8 solid %4;
        }
        QMenu::item {
            padding: 5px 24px 5px 10px;
        }
        QMenu::item:selected {
            background: %10;
            color: %21;
        }
        QMenu::item:disabled {
            color: %11;
        }

        QStatusBar {
            background: %1;
            color: %5;
            border-top: %8 solid %4;
        }
    )")
        .arg(colors.background().name())
        .arg(colors.textPrimary().name())
        .arg(colors.surface().name())
        .arg(colors.border().name())
        .arg(colors.textSecondary().name())
        .arg(colors.inputBackground().name())
        .arg(colors.disabledSurface().name())
        .arg(borderWidth)
        .arg(radius)
        .arg(colors.selection().name())
        .arg(colors.textDisabled().name())
        .arg(colors.focus().name())
        .arg(colors.error().name())
        .arg(colors.accentText().name())
        .arg(colors.accentHover().name())
        .arg(colors.accentPressed().name())
        .arg(progressRadius)
        .arg(colors.accentHoverText().name())
        .arg(colors.accentPressedText().name())
        .arg(focusWidth)
        .arg(colors.selectionText().name())
        .arg(colors.accent().name())
        .arg(colors.buttonBackground().name())
        .arg(colors.buttonHover().name())
        .arg(colors.buttonPressed().name());
}

QIcon windowsGlyphIcon(const QWidget *context,
                       const unsigned short glyph,
                       const int logicalSize)
{
    if (context == nullptr || logicalSize <= 0) {
        return {};
    }

    const QStringList families = QFontDatabase::families();
    if (!families.contains(QStringLiteral("Segoe Fluent Icons"))) {
        return {};
    }

    const qreal ratio = context->devicePixelRatioF();
    const int pixelSize = qMax(1, qRound(logicalSize * ratio));
    QPixmap pixmap(pixelSize, pixelSize);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    QFont font(QStringLiteral("Segoe Fluent Icons"));
    font.setPixelSize(qMax(8, qRound(logicalSize * 0.82 * ratio)));
    painter.setFont(font);
    painter.setPen(context->palette().color(QPalette::WindowText));
    painter.drawText(
        pixmap.rect(),
        Qt::AlignCenter,
        QString(QChar(glyph)));

    return QIcon(pixmap);
}

} // namespace FilePilot
