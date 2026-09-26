#pragma once

#include "ui/theme/ThemeSnapshot.h"

#include <QColor>

namespace FilePilot {

class ThemePalette {
public:
    ThemePalette() = default;

    static ThemePalette fromSnapshot(const ThemeSnapshot &snapshot);

    QColor accent() const;
    QColor accentHover() const;
    QColor accentHoverText() const;
    QColor accentPressed() const;
    QColor accentPressedText() const;
    QColor accentSubtle() const;

    QColor background() const;
    QColor surface() const;
    QColor surfaceSecondary() const;
    QColor control() const;
    QColor controlHover() const;

    QColor border() const;
    QColor textPrimary() const;
    QColor textSecondary() const;
    QColor disabledText() const;
    QColor selection() const;
    QColor selectionText() const;
    QColor accentText() const;
    QColor focus() const;
    QColor error() const;

    bool highContrast() const;
    bool isValid() const;

    bool operator==(const ThemePalette &other) const;
    bool operator!=(const ThemePalette &other) const;

public:
    ThemePalette(ThemeMode mode,
                 bool highContrast,
                 const QColor &accent,
                 const QColor &accentHover,
                 const QColor &accentHoverText,
                 const QColor &accentPressed,
                 const QColor &accentPressedText,
                 const QColor &accentSubtle,
                 const QColor &background,
                 const QColor &surface,
                 const QColor &surfaceSecondary,
                 const QColor &control,
                 const QColor &controlHover,
                 const QColor &border,
                 const QColor &textPrimary,
                 const QColor &textSecondary,
                 const QColor &disabledText,
                 const QColor &selection,
                 const QColor &selectionText,
                 const QColor &accentText,
                 const QColor &focus,
                 const QColor &error);

    ThemeMode mode_ = ThemeMode::Unknown;
    bool highContrast_ = false;
    QColor accent_;
    QColor accentHover_;
    QColor accentHoverText_;
    QColor accentPressed_;
    QColor accentPressedText_;
    QColor accentSubtle_;
    QColor background_;
    QColor surface_;
    QColor surfaceSecondary_;
    QColor control_;
    QColor controlHover_;
    QColor border_;
    QColor textPrimary_;
    QColor textSecondary_;
    QColor disabledText_;
    QColor selection_;
    QColor selectionText_;
    QColor accentText_;
    QColor focus_;
    QColor error_;
};

} // namespace FilePilot
