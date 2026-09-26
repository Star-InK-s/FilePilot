#pragma once

#include "ui/theme/ThemePalette.h"

#include <QIcon>
#include <QString>

class QWidget;

namespace FilePilot {

QString fluentStyleSheet(const ThemePalette &palette);
QIcon windowsGlyphIcon(const QWidget *context,
                       unsigned short glyph,
                       int logicalSize);

} // namespace FilePilot
