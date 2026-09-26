#include "ui/presenters/DuplicateTheme.h"

#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSettings>
#include <QTimer>
#include <QWidget>
#include <QtMath>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace FilePilot {
namespace {

struct FluentColors {
    QColor background;
    QColor surface;
    QColor surfaceAlternate;
    QColor control;
    QColor controlHover;
    QColor border;
    QColor text;
    QColor mutedText;
    QColor disabledText;
    QColor selection;
    QColor selectionText;
    QColor accent;
    QColor accentText;
    QColor accentHover;
    QColor accentPressed;
    QColor focus;
    QColor error;
    bool highContrast = false;
};

QColor blend(const QColor &first, const QColor &second, const qreal amount)
{
    return QColor(
        qRound(first.red() + (second.red() - first.red()) * amount),
        qRound(first.green() + (second.green() - first.green()) * amount),
        qRound(first.blue() + (second.blue() - first.blue()) * amount));
}

qreal linearChannel(const int value)
{
    const qreal normalized = static_cast<qreal>(value) / 255.0;
    return normalized <= 0.04045
        ? normalized / 12.92
        : qPow((normalized + 0.055) / 1.055, 2.4);
}

qreal relativeLuminance(const QColor &color)
{
    return 0.2126 * linearChannel(color.red())
        + 0.7152 * linearChannel(color.green())
        + 0.0722 * linearChannel(color.blue());
}

QColor readableTextColor(const QColor &background)
{
    const QColor dark(QStringLiteral("#1B1B1B"));
    const QColor light(QStringLiteral("#FFFFFF"));
    const qreal darkContrast = (relativeLuminance(background) + 0.05)
        / (relativeLuminance(dark) + 0.05);
    const qreal lightContrast = (relativeLuminance(light) + 0.05)
        / (relativeLuminance(background) + 0.05);
    return darkContrast >= lightContrast ? dark : light;
}

#ifdef Q_OS_WIN
QColor windowsSystemColor(const int index)
{
    const DWORD value = GetSysColor(index);
    return QColor(
        static_cast<int>(value & 0xFFu),
        static_cast<int>((value >> 8u) & 0xFFu),
        static_cast<int>((value >> 16u) & 0xFFu));
}

bool windowsHighContrastEnabled()
{
    HIGHCONTRASTW highContrast{};
    highContrast.cbSize = sizeof(highContrast);
    return SystemParametersInfoW(
               SPI_GETHIGHCONTRAST,
               sizeof(highContrast),
               &highContrast,
               0)
        && (highContrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

QColor windowsAccentColor()
{
    QSettings settings(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\DWM"),
        QSettings::NativeFormat);
    bool ok = false;
    const uint value = settings.value(QStringLiteral("AccentColor")).toUInt(&ok);
    if (!ok) {
        return {};
    }
    return QColor(
        static_cast<int>(value & 0xFFu),
        static_cast<int>((value >> 8u) & 0xFFu),
        static_cast<int>((value >> 16u) & 0xFFu));
}

bool windowsAppsUseDarkTheme()
{
    QSettings settings(
        QStringLiteral(
            "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
        QSettings::NativeFormat);
    return settings.value(QStringLiteral("AppsUseLightTheme"), 1).toInt() == 0;
}
#else
QColor windowsSystemColor(const int)
{
    return {};
}

bool windowsHighContrastEnabled()
{
    return false;
}

QColor windowsAccentColor()
{
    return {};
}

bool windowsAppsUseDarkTheme()
{
    return false;
}
#endif

FluentColors currentColors()
{
    FluentColors colors;
    colors.highContrast = windowsHighContrastEnabled();

    if (colors.highContrast) {
        const QColor window = windowsSystemColor(COLOR_WINDOW);
        const QColor windowText = windowsSystemColor(COLOR_WINDOWTEXT);
        const QColor button = windowsSystemColor(COLOR_BTNFACE);
        const QColor buttonText = windowsSystemColor(COLOR_BTNTEXT);
        const QColor highlight = windowsSystemColor(COLOR_HIGHLIGHT);
        const QColor highlightedText = windowsSystemColor(COLOR_HIGHLIGHTTEXT);
        const QColor hotLight = windowsSystemColor(COLOR_HOTLIGHT);
        const QColor frame = windowsSystemColor(COLOR_WINDOWFRAME);

        colors.background = window;
        colors.surface = window;
        colors.surfaceAlternate = button;
        colors.control = button;
        colors.controlHover = blend(button, buttonText, 0.08);
        colors.border = frame;
        colors.text = windowText;
        colors.mutedText = windowText;
        colors.disabledText = windowsSystemColor(COLOR_GRAYTEXT);
        colors.selection = highlight;
        colors.selectionText = highlightedText;
        colors.accent = button;
        colors.accentText = buttonText;
        colors.accentHover = blend(button, buttonText, 0.12);
        colors.accentPressed = blend(button, buttonText, 0.22);
        colors.focus = hotLight;
        colors.error = windowText;
        return colors;
    }

    QColor accent = windowsAccentColor();
    if (!accent.isValid()) {
        accent = QColor(QStringLiteral("#0067C0"));
    }

    const bool dark = windowsAppsUseDarkTheme();
    colors.background = dark
        ? QColor(QStringLiteral("#202020"))
        : QColor(QStringLiteral("#F3F3F3"));
    colors.surface = dark
        ? QColor(QStringLiteral("#272727"))
        : QColor(QStringLiteral("#FFFFFF"));
    colors.surfaceAlternate = dark
        ? QColor(QStringLiteral("#2B2B2B"))
        : QColor(QStringLiteral("#F7F7F7"));
    colors.control = dark
        ? QColor(QStringLiteral("#2D2D2D"))
        : QColor(QStringLiteral("#FBFBFB"));
    colors.controlHover = dark
        ? QColor(QStringLiteral("#383838"))
        : QColor(QStringLiteral("#F0F0F0"));
    colors.border = dark
        ? QColor(QStringLiteral("#3B3B3B"))
        : QColor(QStringLiteral("#E1E1E1"));
    colors.text = dark
        ? QColor(QStringLiteral("#F5F5F5"))
        : QColor(QStringLiteral("#1A1A1A"));
    colors.mutedText = dark
        ? QColor(QStringLiteral("#C7C7C7"))
        : QColor(QStringLiteral("#5D5D5D"));
    colors.disabledText = dark
        ? QColor(QStringLiteral("#7A7A7A"))
        : QColor(QStringLiteral("#A1A1A1"));
    colors.selection = blend(accent, colors.surface, dark ? 0.82 : 0.88);
    colors.selectionText = colors.text;
    colors.accent = accent;
    colors.accentText = readableTextColor(accent);
    colors.accentHover = blend(accent, dark ? QColor(Qt::white) : QColor(Qt::white), 0.12);
    colors.accentPressed = blend(accent, QColor(Qt::black), 0.12);
    colors.focus = accent;
    colors.error = dark
        ? QColor(QStringLiteral("#FF99A4"))
        : QColor(QStringLiteral("#C42B1C"));
    return colors;
}

QString buildStyleSheet(const FluentColors &colors)
{
    const QString borderWidth = colors.highContrast
        ? QStringLiteral("2px")
        : QStringLiteral("1px");
    const QString focusWidth = QStringLiteral("2px");
    const QString radius = colors.highContrast
        ? QStringLiteral("2px")
        : QStringLiteral("4px");
    const QString progressRadius = colors.highContrast
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
        QWidget#pageDuplicates,
        QStackedWidget#pageStack {
            background: %1;
            color: %2;
        }
        QLabel {
            background: transparent;
        }
        QLabel[errorState="true"] {
            color: %13;
        }
        QToolTip {
            background: %5;
            color: %2;
            border: %8px solid %4;
            padding: 6px;
        }

        QToolBar#mainToolBar {
            background: %1;
            border: 0;
            border-bottom: %8px solid %4;
            spacing: 4px;
            padding: 6px 10px;
        }
        QToolBar#mainToolBar QToolButton {
            background: transparent;
            border: %8px solid transparent;
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
            border-right: %8px solid %4;
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
            border-left: 3px solid %12;
            color: %2;
        }
        QListWidget#navigation::item:focus {
            outline: %7px solid %12;
            outline-offset: -2px;
        }

        QFrame#globalProgressPanel {
            background: %3;
            border: 0;
            border-top: %8px solid %4;
        }
        QFrame#duplicateSummaryPanel {
            background: transparent;
            border: 0;
            border-bottom: %8px solid %4;
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
            border-bottom: %8px solid %4;
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
            border: %8px solid %4;
            border-radius: %9;
            padding: 5px 8px;
            selection-background-color: %12;
            selection-color: %14;
        }
        QLineEdit:focus {
            border: %7px solid %12;
            padding: 4px 7px;
        }
        QLineEdit:disabled {
            background: %7;
            color: %11;
        }

        QPushButton {
            background: %6;
            color: %2;
            border: %8px solid %4;
            border-radius: %9;
            min-height: 30px;
            padding: 3px 12px;
        }
        QPushButton:hover {
            background: %7;
        }
        QPushButton:pressed {
            background: %4;
        }
        QPushButton:focus {
            border: %7px solid %12;
            padding: 2px 11px;
        }
        QPushButton:disabled {
            background: %7;
            color: %11;
            border-color: %4;
        }
        QPushButton#startDuplicateScanButton {
            background: %12;
            color: %14;
            border-color: %12;
            font-weight: 600;
            padding: 3px 16px;
        }
        QPushButton#startDuplicateScanButton:hover {
            background: %15;
            border-color: %15;
        }
        QPushButton#startDuplicateScanButton:pressed {
            background: %16;
            border-color: %16;
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
            background: %12;
            border-radius: %17;
        }

        QAbstractItemView::viewport {
            background: %3;
        }
        QTableView {
            background: %3;
            alternate-background-color: %1;
            color: %2;
            border: %8px solid %4;
            border-radius: %9;
            gridline-color: transparent;
            selection-background-color: %10;
            selection-color: %2;
            outline: 0;
        }
        QTableView::item {
            padding: 6px 8px;
            border: 0;
        }
        QTableView::item:hover {
            background: %6;
        }
        QTableView::item:selected {
            background: %10;
            color: %2;
        }
        QHeaderView::section {
            background: %1;
            color: %5;
            border: 0;
            border-bottom: %8px solid %4;
            padding: 7px 8px;
        }
        QHeaderView::section:hover {
            background: %6;
            color: %2;
        }

        QTabWidget::pane {
            background: %3;
            border: %8px solid %4;
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
            border-bottom: 2px solid %12;
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
            background: %2;
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
            background: %2;
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

        QStatusBar {
            background: %1;
            color: %5;
            border-top: %8px solid %4;
        }
    )")
        .arg(colors.background.name())
        .arg(colors.text.name())
        .arg(colors.surface.name())
        .arg(colors.border.name())
        .arg(colors.mutedText.name())
        .arg(colors.control.name())
        .arg(colors.controlHover.name())
        .arg(borderWidth)
        .arg(radius)
        .arg(colors.selection.name())
        .arg(colors.disabledText.name())
        .arg(colors.focus.name())
        .arg(colors.error.name())
        .arg(colors.accentText.name())
        .arg(colors.accentHover.name())
        .arg(colors.accentPressed.name())
        .arg(progressRadius);
}

class FluentThemeController final
    : public QObject
    , public QAbstractNativeEventFilter
{
public:
    explicit FluentThemeController(QWidget *target)
        : QObject(target)
        , target_(target)
    {
        refreshTimer_.setSingleShot(true);
        connect(&refreshTimer_, &QTimer::timeout, this, [this] { refresh(); });

        qApp->installEventFilter(this);
        qApp->installNativeEventFilter(this);
        refresh();
    }

    ~FluentThemeController() override
    {
        qApp->removeEventFilter(this);
        qApp->removeNativeEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        Q_UNUSED(watched)
        switch (event->type()) {
        case QEvent::ApplicationPaletteChange:
        case QEvent::PaletteChange:
        case QEvent::StyleChange:
        case QEvent::ThemeChange:
        case QEvent::FontChange:
        case QEvent::ScreenChangeInternal:
            refreshTimer_.start(0);
            break;
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }

#ifdef Q_OS_WIN
    bool nativeEventFilter(const QByteArray &eventType,
                           void *message,
                           qintptr *result) override
    {
        Q_UNUSED(eventType)
        Q_UNUSED(result)
        const auto *nativeMessage = static_cast<const MSG *>(message);
        switch (nativeMessage->message) {
        case WM_SETTINGCHANGE:
        case WM_SYSCOLORCHANGE:
        case WM_THEMECHANGED:
        case WM_DWMCOLORIZATIONCOLORCHANGED:
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            refreshTimer_.start(40);
            break;
        default:
            break;
        }
        return false;
    }
#endif

private:
    void refresh()
    {
        if (target_ == nullptr) {
            return;
        }

        const FluentColors colors = currentColors();

        QPalette palette = target_->palette();
        palette.setColor(QPalette::Window, colors.background);
        palette.setColor(QPalette::WindowText, colors.text);
        palette.setColor(QPalette::Base, colors.surface);
        palette.setColor(QPalette::AlternateBase, colors.surfaceAlternate);
        palette.setColor(QPalette::Text, colors.text);
        palette.setColor(QPalette::Button, colors.control);
        palette.setColor(QPalette::ButtonText, colors.text);
        palette.setColor(QPalette::Highlight, colors.selection);
        palette.setColor(QPalette::HighlightedText, colors.selectionText);
        palette.setColor(QPalette::ToolTipBase, colors.surface);
        palette.setColor(QPalette::ToolTipText, colors.text);
        palette.setColor(QPalette::PlaceholderText, colors.mutedText);
        palette.setColor(QPalette::Disabled, QPalette::Text, colors.disabledText);
        palette.setColor(QPalette::Disabled, QPalette::WindowText, colors.disabledText);
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, colors.disabledText);
        if (QApplication::palette() != palette) {
            QApplication::setPalette(palette);
        }
        if (target_->palette() != palette) {
            target_->setPalette(palette);
        }

        const QList<QWidget *> children = target_->findChildren<QWidget *>();
        for (QWidget *child : children) {
            if (child->palette() != palette) {
                child->setPalette(palette);
            }
        }

        QFont font = target_->font();
        font.setFamilies({
            QStringLiteral("Segoe UI Variable Text"),
            QStringLiteral("Segoe UI"),
            QStringLiteral("Microsoft YaHei UI"),
        });
        if (target_->font() != font) {
            target_->setFont(font);
        }

        const QString styleSheet = buildStyleSheet(colors);
        if (target_->styleSheet() != styleSheet) {
            target_->setStyleSheet(styleSheet);
        }
        target_->update();
    }

    QWidget *target_ = nullptr;
    QTimer refreshTimer_;
};

} // namespace

void applyWindowsTheme(QWidget *window)
{
    if (window != nullptr) {
        new FluentThemeController(window);
    }
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
