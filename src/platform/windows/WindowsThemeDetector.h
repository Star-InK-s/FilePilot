#pragma once

#include "ui/theme/ThemeSnapshot.h"

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QTimer>

#include <functional>
#include <optional>

namespace FilePilot {

class WindowsThemeDetector final
    : public QObject
    , public QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    using SnapshotReader = std::function<ThemeSnapshot()>;

    explicit WindowsThemeDetector(QObject *parent = nullptr);
    explicit WindowsThemeDetector(SnapshotReader snapshotReader,
                                  QObject *parent = nullptr);
    ~WindowsThemeDetector() override;

    ThemeSnapshot snapshot() const;
    void start();
    void refreshNow();

    static ThemeMode resolveMode(
        const std::optional<bool> &appsUseLightTheme,
        const std::optional<bool> &systemUsesLightTheme,
        ThemeMode qtPaletteMode);

signals:
    void themeChanged(const FilePilot::ThemeSnapshot &snapshot);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

#ifdef Q_OS_WIN
    bool nativeEventFilter(const QByteArray &eventType,
                           void *message,
                           qintptr *result) override;
#endif

private:
    void scheduleRefresh();
    void refresh();

    SnapshotReader snapshotReader_;
    QTimer refreshTimer_;
    ThemeSnapshot lastSnapshot_;
    bool started_ = false;
    bool hasLastSnapshot_ = false;
};

} // namespace FilePilot
