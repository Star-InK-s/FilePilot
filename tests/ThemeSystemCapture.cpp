#include "app/Application.h"
#include "app/MainWindow.h"

#include <QCoreApplication>
#include <QDir>
#include <QListWidget>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <cstdlib>
#include <string>

namespace {

void broadcastThemeChange()
{
#ifdef Q_OS_WIN
    SendMessageTimeoutW(HWND_BROADCAST,
                        WM_SETTINGCHANGE,
                        0,
                        reinterpret_cast<LPARAM>(L"ImmersiveColorSet"),
                        SMTO_ABORTIFHUNG,
                        250,
                        nullptr);
    SendMessageTimeoutW(HWND_BROADCAST,
                        WM_SYSCOLORCHANGE,
                        0,
                        0,
                        SMTO_ABORTIFHUNG,
                        250,
                        nullptr);
#endif
    QCoreApplication::processEvents();
    QThread::msleep(250);
    QCoreApplication::processEvents();
}

struct PersonalizeState {
    QVariant appsUseLightTheme;
    QVariant systemUsesLightTheme;
};

QString personalizeKey()
{
    return QStringLiteral(
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize");
}

PersonalizeState readPersonalizeState()
{
    QSettings settings(personalizeKey(), QSettings::NativeFormat);
    return {
        settings.value(QStringLiteral("AppsUseLightTheme")),
        settings.value(QStringLiteral("SystemUsesLightTheme")),
    };
}

void setAppsLightTheme(const bool light)
{
    QSettings settings(personalizeKey(), QSettings::NativeFormat);
    settings.setValue(QStringLiteral("AppsUseLightTheme"), light ? 1 : 0);
    settings.setValue(QStringLiteral("SystemUsesLightTheme"), light ? 1 : 0);
    settings.sync();
    broadcastThemeChange();
}

QString dwmKey()
{
    return QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\DWM");
}

struct AccentState {
    QVariant accentColor;
    QVariant colorizationColor;
    bool hasAccentColor = false;
    bool hasColorizationColor = false;
};

AccentState readAccentState()
{
    QSettings settings(dwmKey(), QSettings::NativeFormat);
    AccentState result;
    result.hasAccentColor =
        settings.contains(QStringLiteral("AccentColor"));
    result.hasColorizationColor =
        settings.contains(QStringLiteral("ColorizationColor"));
    result.accentColor =
        settings.value(QStringLiteral("AccentColor"));
    result.colorizationColor =
        settings.value(QStringLiteral("ColorizationColor"));
    return result;
}

void setAccentColor(const uint packed)
{
    QSettings settings(dwmKey(), QSettings::NativeFormat);
    settings.setValue(QStringLiteral("AccentColor"), packed);
    settings.setValue(QStringLiteral("ColorizationColor"), packed);
    settings.sync();
#ifdef Q_OS_WIN
    SendMessageTimeoutW(HWND_BROADCAST,
                        WM_DWMCOLORIZATIONCOLORCHANGED,
                        0,
                        0,
                        SMTO_ABORTIFHUNG,
                        250,
                        nullptr);
#endif
    QCoreApplication::processEvents();
    QThread::msleep(250);
    QCoreApplication::processEvents();
}

void restoreAccentState(const AccentState &state)
{
    QSettings settings(dwmKey(), QSettings::NativeFormat);
    if (state.hasAccentColor) {
        settings.setValue(
            QStringLiteral("AccentColor"),
            state.accentColor);
    } else {
        settings.remove(QStringLiteral("AccentColor"));
    }
    if (state.hasColorizationColor) {
        settings.setValue(
            QStringLiteral("ColorizationColor"),
            state.colorizationColor);
    } else {
        settings.remove(QStringLiteral("ColorizationColor"));
    }
    settings.sync();
#ifdef Q_OS_WIN
    SendMessageTimeoutW(HWND_BROADCAST,
                        WM_DWMCOLORIZATIONCOLORCHANGED,
                        0,
                        0,
                        SMTO_ABORTIFHUNG,
                        250,
                        nullptr);
#endif
}

void restorePersonalizeState(const PersonalizeState &state)
{
    QSettings settings(personalizeKey(), QSettings::NativeFormat);
    settings.setValue(
        QStringLiteral("AppsUseLightTheme"),
        state.appsUseLightTheme);
    settings.setValue(
        QStringLiteral("SystemUsesLightTheme"),
        state.systemUsesLightTheme);
    settings.sync();
    broadcastThemeChange();
}

#ifdef Q_OS_WIN
struct HighContrastState {
    bool enabled = false;
    DWORD flags = 0;
    std::wstring scheme;
};

HighContrastState readHighContrast()
{
    HighContrastState result;
    result.scheme.assign(256, L'\0');
    HIGHCONTRASTW state{};
    state.cbSize = sizeof(state);
    state.lpszDefaultScheme = result.scheme.data();
    if (SystemParametersInfoW(
            SPI_GETHIGHCONTRAST,
            sizeof(state),
            &state,
            0)) {
        result.enabled = (state.dwFlags & HCF_HIGHCONTRASTON) != 0;
        result.flags = state.dwFlags;
        result.scheme.resize(wcslen(result.scheme.c_str()));
    }
    return result;
}

void setHighContrast(const HighContrastState &state)
{
    HIGHCONTRASTW target{};
    target.cbSize = sizeof(target);
    target.dwFlags = state.flags;
    std::wstring scheme = state.scheme.empty()
        ? std::wstring(L"High Contrast Black")
        : state.scheme;
    target.lpszDefaultScheme = scheme.data();
    SystemParametersInfoW(
        SPI_SETHIGHCONTRAST,
        sizeof(target),
        &target,
        SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    broadcastThemeChange();
}
#endif

QString pageName(const int row)
{
    switch (row) {
    case 0:
        return QStringLiteral("file-organize");
    case 1:
        return QStringLiteral("duplicate-files");
    case 2:
        return QStringLiteral("backup");
    case 3:
        return QStringLiteral("history");
    default:
        return QStringLiteral("settings");
    }
}

void capturePages(FilePilot::MainWindow &window,
                  QListWidget *navigation,
                  const QString &outputDirectory,
                  const QString &mode)
{
    QDir().mkpath(outputDirectory);
    for (int row = 0; row < navigation->count(); ++row) {
        navigation->setCurrentRow(row);
        QCoreApplication::processEvents();
        QThread::msleep(120);
        QCoreApplication::processEvents();
        const QString path = QDir(outputDirectory).filePath(
            QStringLiteral("%1-%2.png").arg(mode, pageName(row)));
        window.grab().save(path);
    }
}

} // namespace

int main(int argc, char *argv[])
{
    const QString outputDirectory = argc > 1
        ? QString::fromLocal8Bit(argv[1])
        : QDir(QStandardPaths::writableLocation(
                   QStandardPaths::TempLocation))
              .filePath(QStringLiteral("filepilot-theme-screenshots"));

    const PersonalizeState originalPersonalize = readPersonalizeState();
    const AccentState originalAccent = readAccentState();
#ifdef Q_OS_WIN
    const HighContrastState originalHighContrast = readHighContrast();
#endif

    QTemporaryDir applicationData;
    if (!applicationData.isValid()) {
        return 1;
    }

    FilePilot::Application application(
        argc,
        argv,
        applicationData.filePath(QStringLiteral("data")));
    FilePilot::MainWindow window(application);
    window.resize(1280, 820);
    window.show();

    auto *navigation = window.findChild<QListWidget *>(
        QStringLiteral("navigation"));
    if (navigation == nullptr) {
        return 2;
    }

    setAppsLightTheme(true);
    capturePages(window, navigation, outputDirectory, QStringLiteral("light"));

    setAppsLightTheme(false);
    capturePages(window, navigation, outputDirectory, QStringLiteral("dark"));

    setAppsLightTheme(true);
    setAccentColor(0x0000FF00u);
    capturePages(
        window,
        navigation,
        outputDirectory,
        QStringLiteral("accent-green"));

#ifdef Q_OS_WIN
    HighContrastState highContrast = originalHighContrast;
    highContrast.flags |= HCF_HIGHCONTRASTON;
    setHighContrast(highContrast);
    capturePages(
        window,
        navigation,
        outputDirectory,
        QStringLiteral("high-contrast"));

    setHighContrast(originalHighContrast);
#endif

    restoreAccentState(originalAccent);
    restorePersonalizeState(originalPersonalize);
    return 0;
}
