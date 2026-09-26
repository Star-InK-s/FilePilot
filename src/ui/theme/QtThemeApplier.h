#pragma once

#include "ui/theme/ThemePalette.h"

class QWidget;

namespace FilePilot {

class QtThemeApplier {
public:
    explicit QtThemeApplier(QWidget *target);

    bool apply(const ThemePalette &palette);
    bool isApplying() const;
    const ThemePalette &palette() const;

private:
    QWidget *target_ = nullptr;
    ThemePalette palette_;
    bool hasPalette_ = false;
    bool applying_ = false;
};

} // namespace FilePilot
