#pragma once

#include <QIcon>

class QWidget;

namespace FilePilot {

void applyWindowsTheme(QWidget *window);
QIcon windowsGlyphIcon(const QWidget *context,
                       unsigned short glyph,
                       int logicalSize);

} // namespace FilePilot
