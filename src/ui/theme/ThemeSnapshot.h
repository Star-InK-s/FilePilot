#pragma once

#include <QColor>
#include <QMetaType>
#include <QString>

namespace FilePilot {

enum class ThemeMode {
    Light,
    Dark,
    Unknown,
};

struct ThemeSnapshot {
    ThemeMode mode = ThemeMode::Unknown;
    bool highContrast = false;

    QColor accent;
    QColor background;
    QColor foreground;
    QColor highlight;
    QColor highlightText;

    QColor buttonFace;
    QColor buttonText;
    QColor hotLight;
    QColor windowFrame;
    QColor grayText;

    bool valid = false;
    QString baseThemeName;
    QString currentThemeType;
};

bool operator==(const ThemeSnapshot &left, const ThemeSnapshot &right);
bool operator!=(const ThemeSnapshot &left, const ThemeSnapshot &right);

} // namespace FilePilot

Q_DECLARE_METATYPE(FilePilot::ThemeSnapshot)
