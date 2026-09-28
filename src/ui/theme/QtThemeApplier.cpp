#include "ui/theme/QtThemeApplier.h"

#include "ui/theme/ThemeStyleSheet.h"

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QThread>
#include <QWidget>

#include <QtGlobal>

namespace FilePilot {

QtThemeApplier::QtThemeApplier(QWidget *target)
    : target_(target)
{
}

bool QtThemeApplier::apply(const ThemePalette &palette)
{
    if (target_ == nullptr || applying_ || !palette.isValid()) {
        return false;
    }

    Q_ASSERT(target_->thread() == QThread::currentThread());
    if (hasPalette_ && palette_ == palette) {
        return false;
    }

    applying_ = true;

    QPalette widgetPalette = target_->palette();
    widgetPalette.setColor(QPalette::Window, palette.background());
    widgetPalette.setColor(QPalette::WindowText, palette.textPrimary());
    widgetPalette.setColor(QPalette::Base, palette.surface());
    widgetPalette.setColor(QPalette::AlternateBase, palette.surfaceSecondary());
    widgetPalette.setColor(QPalette::Text, palette.textPrimary());
    widgetPalette.setColor(QPalette::Button, palette.buttonBackground());
    widgetPalette.setColor(QPalette::ButtonText, palette.accentText());
    widgetPalette.setColor(QPalette::Highlight, palette.selection());
    widgetPalette.setColor(QPalette::HighlightedText, palette.selectionText());
    widgetPalette.setColor(QPalette::ToolTipBase, palette.surface());
    widgetPalette.setColor(QPalette::ToolTipText, palette.textPrimary());
    widgetPalette.setColor(QPalette::PlaceholderText, palette.textSecondary());
    widgetPalette.setColor(QPalette::Link, palette.focus());
    widgetPalette.setColor(QPalette::LinkVisited, palette.focus());

    widgetPalette.setColor(QPalette::Disabled, QPalette::Window, palette.disabledSurface());
    widgetPalette.setColor(QPalette::Disabled, QPalette::WindowText, palette.textDisabled());
    widgetPalette.setColor(QPalette::Disabled, QPalette::Base, palette.disabledSurface());
    widgetPalette.setColor(QPalette::Disabled, QPalette::AlternateBase, palette.disabledSurface());
    widgetPalette.setColor(QPalette::Disabled, QPalette::Text, palette.textDisabled());
    widgetPalette.setColor(QPalette::Disabled, QPalette::Button, palette.disabledSurface());
    widgetPalette.setColor(QPalette::Disabled, QPalette::ButtonText, palette.textDisabled());
    widgetPalette.setColor(QPalette::Disabled, QPalette::Highlight, palette.selection());
    widgetPalette.setColor(QPalette::Disabled, QPalette::HighlightedText, palette.selectionText());
    widgetPalette.setColor(QPalette::Disabled, QPalette::ToolTipBase, palette.disabledSurface());
    widgetPalette.setColor(QPalette::Disabled, QPalette::ToolTipText, palette.textDisabled());
    widgetPalette.setColor(QPalette::Disabled, QPalette::PlaceholderText, palette.textDisabled());

    if (QApplication::instance() != nullptr
        && QApplication::palette() != widgetPalette) {
        QApplication::setPalette(widgetPalette);
    }
    if (target_->palette() != widgetPalette) {
        target_->setPalette(widgetPalette);
    }

    const QString styleSheet = fluentStyleSheet(palette);
    if (target_->styleSheet() != styleSheet) {
        target_->setStyleSheet(styleSheet);
    }
    target_->update();

    palette_ = palette;
    hasPalette_ = true;
    applying_ = false;
    return true;
}

bool QtThemeApplier::isApplying() const
{
    return applying_;
}

const ThemePalette &QtThemeApplier::palette() const
{
    return palette_;
}

} // namespace FilePilot
