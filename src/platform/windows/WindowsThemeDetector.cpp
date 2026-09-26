#include "platform/windows/WindowsThemeDetector.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QPalette>
#include <QSettings>
#include <QThread>

#include <optional>
#include <utility>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace FilePilot {
namespace {

ThemeMode modeFromLightFlag(const bool usesLightTheme)
{
    return usesLightTheme ? ThemeMode::Light : ThemeMode::Dark;
}

ThemeMode qtPaletteMode()
{
    if (QGuiApplication::instance() != nullptr) {
        const QColor window =
            QGuiApplication::palette().color(QPalette::Window);
        if (window.isValid()) {
            const qreal luminance = 0.2126 * window.redF()
                + 0.7152 * window.greenF()
                + 0.0722 * window.blueF();
            return luminance < 0.5 ? ThemeMode::Dark : ThemeMode::Light;
        }
    }

    return ThemeMode::Unknown;
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

std::optional<uint> readRegistryDword(const QString &path,
                                      const QString &name)
{
    QSettings settings(path, QSettings::NativeFormat);
    bool ok = false;
    const uint value = settings.value(name).toUInt(&ok);
    return ok ? std::optional<uint>(value) : std::nullopt;
}

std::optional<QColor> readAccentColor()
{
    const QString path =
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\DWM");
    if (const auto value = readRegistryDword(path, QStringLiteral("AccentColor"))) {
        const uint packed = *value;
        return QColor(
            static_cast<int>(packed & 0xFFu),
            static_cast<int>((packed >> 8u) & 0xFFu),
            static_cast<int>((packed >> 16u) & 0xFFu));
    }

    if (const auto value = readRegistryDword(path, QStringLiteral("ColorizationColor"))) {
        return QColor::fromRgba(static_cast<QRgb>(*value));
    }

    return std::nullopt;
}

std::optional<bool> readAppsUseLightTheme()
{
    const auto value = readRegistryDword(
        QStringLiteral(
            "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
        QStringLiteral("AppsUseLightTheme"));
    return value ? std::optional<bool>(*value != 0) : std::nullopt;
}

std::optional<bool> readSystemUsesLightTheme()
{
    const auto value = readRegistryDword(
        QStringLiteral(
            "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
        QStringLiteral("SystemUsesLightTheme"));
    return value ? std::optional<bool>(*value != 0) : std::nullopt;
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

std::optional<QColor> readAccentColor()
{
    return std::nullopt;
}

std::optional<bool> readAppsUseLightTheme()
{
    return std::nullopt;
}

std::optional<bool> readSystemUsesLightTheme()
{
    return std::nullopt;
}
#endif

ThemeMode readMode()
{
    return WindowsThemeDetector::resolveMode(
        readAppsUseLightTheme(),
        readSystemUsesLightTheme(),
        qtPaletteMode());
}

} // namespace

ThemeMode WindowsThemeDetector::resolveMode(
    const std::optional<bool> &appsUseLightTheme,
    const std::optional<bool> &systemUsesLightTheme,
    const ThemeMode qtPaletteMode)
{
    if (appsUseLightTheme.has_value()) {
        return modeFromLightFlag(*appsUseLightTheme);
    }
    if (systemUsesLightTheme.has_value()) {
        return modeFromLightFlag(*systemUsesLightTheme);
    }
    return qtPaletteMode;
}

WindowsThemeDetector::WindowsThemeDetector(QObject *parent)
    : WindowsThemeDetector(SnapshotReader{}, parent)
{
}

WindowsThemeDetector::WindowsThemeDetector(SnapshotReader snapshotReader,
                                           QObject *parent)
    : QObject(parent)
    , snapshotReader_(std::move(snapshotReader))
{
    refreshTimer_.setSingleShot(true);
    connect(&refreshTimer_, &QTimer::timeout, this, &WindowsThemeDetector::refresh);
}

WindowsThemeDetector::~WindowsThemeDetector()
{
    if (started_ && QCoreApplication::instance() != nullptr) {
        qApp->removeEventFilter(this);
        qApp->removeNativeEventFilter(this);
    }
}

ThemeSnapshot WindowsThemeDetector::snapshot() const
{
    if (snapshotReader_) {
        return snapshotReader_();
    }

    ThemeSnapshot result;
    result.mode = readMode();
    result.highContrast = windowsHighContrastEnabled();

#ifdef Q_OS_WIN
    result.accent = readAccentColor().value_or(QColor());
    result.background = windowsSystemColor(COLOR_WINDOW);
    result.foreground = windowsSystemColor(COLOR_WINDOWTEXT);
    result.highlight = windowsSystemColor(COLOR_HIGHLIGHT);
    result.highlightText = windowsSystemColor(COLOR_HIGHLIGHTTEXT);
    result.buttonFace = windowsSystemColor(COLOR_BTNFACE);
    result.buttonText = windowsSystemColor(COLOR_BTNTEXT);
    result.hotLight = windowsSystemColor(COLOR_HOTLIGHT);
    result.windowFrame = windowsSystemColor(COLOR_WINDOWFRAME);
    result.grayText = windowsSystemColor(COLOR_GRAYTEXT);
#else
    result.accent = {};
#endif

    result.valid = result.mode != ThemeMode::Unknown
        || result.highContrast
        || result.accent.isValid();
    return result;
}

void WindowsThemeDetector::start()
{
    if (started_ || QCoreApplication::instance() == nullptr) {
        return;
    }

    started_ = true;
    qApp->installEventFilter(this);
    qApp->installNativeEventFilter(this);
    refresh();
}

bool WindowsThemeDetector::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched)

    switch (event->type()) {
    case QEvent::ApplicationPaletteChange:
    case QEvent::PaletteChange:
    case QEvent::StyleChange:
    case QEvent::ThemeChange:
    case QEvent::ScreenChangeInternal:
        scheduleRefresh();
        break;
    default:
        break;
    }

    return QObject::eventFilter(watched, event);
}

#ifdef Q_OS_WIN
bool WindowsThemeDetector::nativeEventFilter(const QByteArray &eventType,
                                             void *message,
                                             qintptr *result)
{
    Q_UNUSED(eventType)
    Q_UNUSED(result)

    const auto *nativeMessage = static_cast<const MSG *>(message);
    if (nativeMessage == nullptr) {
        return false;
    }

    switch (nativeMessage->message) {
    case WM_SETTINGCHANGE:
    case WM_SYSCOLORCHANGE:
    case WM_THEMECHANGED:
    case WM_DWMCOLORIZATIONCOLORCHANGED:
    case WM_DPICHANGED:
        scheduleRefresh();
        break;
    default:
        break;
    }

    return false;
}
#endif

void WindowsThemeDetector::refreshNow()
{
    refresh();
}

void WindowsThemeDetector::scheduleRefresh()
{
    refreshTimer_.start(40);
}

void WindowsThemeDetector::refresh()
{
    Q_ASSERT(QCoreApplication::instance() == nullptr
             || QThread::currentThread() == QCoreApplication::instance()->thread());

    const ThemeSnapshot next = snapshot();
    if (hasLastSnapshot_ && next == lastSnapshot_) {
        return;
    }

    hasLastSnapshot_ = true;
    lastSnapshot_ = next;
    emit themeChanged(next);
}

} // namespace FilePilot
