#include "ui/theme/ThemePalette.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace FilePilot {
namespace {

struct OkLab {
    qreal l = 0.0;
    qreal a = 0.0;
    qreal b = 0.0;
};

qreal linearChannel(const qreal value)
{
    return value <= 0.04045
        ? value / 12.92
        : qPow((value + 0.055) / 1.055, 2.4);
}

qreal srgbChannel(const qreal value)
{
    const qreal clamped = std::clamp(value, 0.0, 1.0);
    return clamped <= 0.0031308
        ? clamped * 12.92
        : 1.055 * qPow(clamped, 1.0 / 2.4) - 0.055;
}

qreal red(const QColor &color)
{
    return linearChannel(color.redF());
}

qreal green(const QColor &color)
{
    return linearChannel(color.greenF());
}

qreal blue(const QColor &color)
{
    return linearChannel(color.blueF());
}

OkLab toOkLab(const QColor &color)
{
    const qreal r = red(color);
    const qreal g = green(color);
    const qreal b = blue(color);

    const qreal l = 0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b;
    const qreal m = 0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b;
    const qreal s = 0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b;

    const qreal lRoot = std::cbrt(std::max<qreal>(0.0, l));
    const qreal mRoot = std::cbrt(std::max<qreal>(0.0, m));
    const qreal sRoot = std::cbrt(std::max<qreal>(0.0, s));

    return {
        0.2104542553 * lRoot + 0.7936177850 * mRoot - 0.0040720468 * sRoot,
        1.9779984951 * lRoot - 2.4285922050 * mRoot + 0.4505937099 * sRoot,
        0.0259040371 * lRoot + 0.7827717662 * mRoot - 0.8086757660 * sRoot,
    };
}

QColor fromOkLab(const OkLab &lab)
{
    const qreal lRoot = lab.l + 0.3963377774 * lab.a + 0.2158037573 * lab.b;
    const qreal mRoot = lab.l - 0.1055613458 * lab.a - 0.0638541728 * lab.b;
    const qreal sRoot = lab.l - 0.0894841775 * lab.a - 1.2914855480 * lab.b;

    const qreal l = lRoot * lRoot * lRoot;
    const qreal m = mRoot * mRoot * mRoot;
    const qreal s = sRoot * sRoot * sRoot;

    const qreal r = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
    const qreal g = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
    const qreal b = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;

    return QColor::fromRgbF(
        std::clamp<float>(static_cast<float>(srgbChannel(r)), 0.0f, 1.0f),
        std::clamp<float>(static_cast<float>(srgbChannel(g)), 0.0f, 1.0f),
        std::clamp<float>(static_cast<float>(srgbChannel(b)), 0.0f, 1.0f));
}

QColor mix(const QColor &first, const QColor &second, const qreal amount)
{
    const OkLab firstLab = toOkLab(first);
    const OkLab secondLab = toOkLab(second);
    return fromOkLab({
        firstLab.l + (secondLab.l - firstLab.l) * amount,
        firstLab.a + (secondLab.a - firstLab.a) * amount,
        firstLab.b + (secondLab.b - firstLab.b) * amount,
    });
}

qreal relativeLuminance(const QColor &color)
{
    return 0.2126 * red(color)
        + 0.7152 * green(color)
        + 0.0722 * blue(color);
}

qreal contrastRatio(const QColor &first, const QColor &second)
{
    const qreal firstLuminance = relativeLuminance(first);
    const qreal secondLuminance = relativeLuminance(second);
    const qreal lighter = std::max(firstLuminance, secondLuminance);
    const qreal darker = std::min(firstLuminance, secondLuminance);
    return (lighter + 0.05) / (darker + 0.05);
}

QColor readableText(const QColor &background)
{
    const QColor black(QStringLiteral("#000000"));
    const QColor white(QStringLiteral("#FFFFFF"));
    return contrastRatio(black, background) >= contrastRatio(white, background)
        ? black
        : white;
}

QColor adjustForContrast(const QColor &foreground,
                         const QColor &background,
                         const qreal target)
{
    const QColor readable = readableText(background);
    if (contrastRatio(foreground, background) >= target) {
        return foreground;
    }

    OkLab lab = toOkLab(foreground);
    const qreal direction = relativeLuminance(foreground) >= relativeLuminance(background)
        ? 1.0
        : -1.0;
    for (int index = 0; index < 32; ++index) {
        lab.l = std::clamp<qreal>(lab.l + direction * 0.025, 0.0, 1.0);
        const QColor candidate = fromOkLab(lab);
        if (contrastRatio(candidate, background) >= target) {
            return candidate;
        }
    }

    return contrastRatio(readable, background) >= contrastRatio(foreground, background)
        ? readable
        : foreground;
}

QColor accessibleText(const QColor &background)
{
    return adjustForContrast(readableText(background), background, 4.5);
}

QColor fallbackBackground(const ThemeMode mode)
{
    return mode == ThemeMode::Dark
        ? QColor(QStringLiteral("#202020"))
        : QColor(QStringLiteral("#F3F3F3"));
}

QColor fallbackForeground(const ThemeMode mode)
{
    return mode == ThemeMode::Dark
        ? QColor(QStringLiteral("#F5F5F5"))
        : QColor(QStringLiteral("#1A1A1A"));
}

QColor fallbackSurface(const ThemeMode mode)
{
    return mode == ThemeMode::Dark
        ? QColor(QStringLiteral("#272727"))
        : QColor(QStringLiteral("#FFFFFF"));
}

QColor fallbackSurfaceSecondary(const ThemeMode mode)
{
    return mode == ThemeMode::Dark
        ? QColor(QStringLiteral("#2B2B2B"))
        : QColor(QStringLiteral("#F7F7F7"));
}

QColor fallbackControl(const ThemeMode mode)
{
    return mode == ThemeMode::Dark
        ? QColor(QStringLiteral("#2D2D2D"))
        : QColor(QStringLiteral("#FBFBFB"));
}

QColor fallbackControlHover(const ThemeMode mode)
{
    return mode == ThemeMode::Dark
        ? QColor(QStringLiteral("#383838"))
        : QColor(QStringLiteral("#F0F0F0"));
}

QColor fallbackBorder(const ThemeMode mode)
{
    return mode == ThemeMode::Dark
        ? QColor(QStringLiteral("#3B3B3B"))
        : QColor(QStringLiteral("#E1E1E1"));
}

QColor fallbackError(const ThemeMode mode)
{
    return mode == ThemeMode::Dark
        ? QColor(QStringLiteral("#FF99A4"))
        : QColor(QStringLiteral("#C42B1C"));
}

QColor safeColor(const QColor &color, const QColor &fallback)
{
    return color.isValid() ? color : fallback;
}

ThemePalette highContrastPalette(const ThemeSnapshot &snapshot)
{
    const QColor background = safeColor(snapshot.background, QColor(Qt::black));
    const QColor foreground = safeColor(snapshot.foreground, QColor(Qt::white));
    const QColor buttonFace = safeColor(snapshot.buttonFace, background);
    const QColor buttonText = safeColor(snapshot.buttonText, foreground);
    const QColor highlight = safeColor(snapshot.highlight, foreground);
    const QColor highlightText = safeColor(snapshot.highlightText, background);
    const QColor hotLight = safeColor(snapshot.hotLight, highlight);
    const QColor windowFrame = safeColor(snapshot.windowFrame, foreground);

    return ThemePalette(
        snapshot.mode,
        true,
        highlight,
        highlight,
        highlightText,
        highlight,
        highlightText,
        background,
        background,
        background,
        buttonFace,
        buttonFace,
        buttonFace,
        windowFrame,
        foreground,
        buttonText,
        safeColor(snapshot.grayText, buttonText),
        highlight,
        highlightText,
        highlightText,
        hotLight,
        foreground);
}

} // namespace

bool operator==(const ThemeSnapshot &left, const ThemeSnapshot &right)
{
    return left.mode == right.mode
        && left.highContrast == right.highContrast
        && left.accent == right.accent
        && left.background == right.background
        && left.foreground == right.foreground
        && left.highlight == right.highlight
        && left.highlightText == right.highlightText
        && left.buttonFace == right.buttonFace
        && left.buttonText == right.buttonText
        && left.hotLight == right.hotLight
        && left.windowFrame == right.windowFrame
        && left.grayText == right.grayText
        && left.valid == right.valid
        && left.baseThemeName == right.baseThemeName
        && left.currentThemeType == right.currentThemeType;
}

bool operator!=(const ThemeSnapshot &left, const ThemeSnapshot &right)
{
    return !(left == right);
}

ThemePalette::ThemePalette(const ThemeMode mode,
                           const bool highContrast,
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
                           const QColor &error)
    : mode_(mode)
    , highContrast_(highContrast)
    , accent_(accent)
    , accentHover_(accentHover)
    , accentHoverText_(accentHoverText)
    , accentPressed_(accentPressed)
    , accentPressedText_(accentPressedText)
    , accentSubtle_(accentSubtle)
    , background_(background)
    , surface_(surface)
    , surfaceSecondary_(surfaceSecondary)
    , control_(control)
    , controlHover_(controlHover)
    , border_(border)
    , textPrimary_(textPrimary)
    , textSecondary_(textSecondary)
    , disabledText_(disabledText)
    , selection_(selection)
    , selectionText_(selectionText)
    , accentText_(accentText)
    , focus_(focus)
    , error_(error)
{
}

ThemePalette ThemePalette::fromSnapshot(const ThemeSnapshot &snapshot)
{
    if (snapshot.highContrast) {
        return highContrastPalette(snapshot);
    }

    const ThemeMode mode = snapshot.mode == ThemeMode::Unknown
        ? ThemeMode::Light
        : snapshot.mode;

    const QColor background = fallbackBackground(mode);
    const QColor foreground = fallbackForeground(mode);
    const QColor surface = fallbackSurface(mode);
    const QColor surfaceSecondary = fallbackSurfaceSecondary(mode);
    const QColor control = fallbackControl(mode);
    const QColor controlHover = fallbackControlHover(mode);
    const QColor border = fallbackBorder(mode);
    const QColor textPrimary = foreground;
    const QColor textSecondary = adjustForContrast(
        mix(foreground, background, 0.22),
        background,
        4.5);
    const QColor disabledText = mix(foreground, background, 0.42);
    const QColor accent = safeColor(snapshot.accent, QColor(QStringLiteral("#0067C0")));
    const QColor accentText = accessibleText(accent);
    const QColor accentHover = mix(accent, accentText, 0.12);
    const QColor accentHoverText = accessibleText(accentHover);
    const QColor accentPressed = mix(accent, accentText, 0.22);
    const QColor accentPressedText = accessibleText(accentPressed);
    const QColor accentSubtle = mix(accent, background, 0.90);
    const QColor selection = mix(accent, surface, mode == ThemeMode::Dark ? 0.78 : 0.88);
    const QColor selectionText = accessibleText(selection);
    const QColor error = adjustForContrast(fallbackError(mode), surface, 4.5);

    return ThemePalette(
        mode,
        false,
        accent,
        accentHover,
        accentHoverText,
        accentPressed,
        accentPressedText,
        accentSubtle,
        background,
        surface,
        surfaceSecondary,
        control,
        controlHover,
        border,
        textPrimary,
        textSecondary,
        disabledText,
        selection,
        selectionText,
        accentText,
        accent,
        error);
}

QColor ThemePalette::surfaceElevated() const { return surface_; }
QColor ThemePalette::textDisabled() const { return disabledText_; }
QColor ThemePalette::borderSubtle() const { return border_; }
QColor ThemePalette::disabledSurface() const { return controlHover_; }
QColor ThemePalette::tableHeader() const { return background_; }
QColor ThemePalette::tableRow() const { return surface_; }
QColor ThemePalette::tableAlternateRow() const { return surfaceSecondary_; }
QColor ThemePalette::inputBackground() const { return control_; }
QColor ThemePalette::inputBorder() const { return border_; }
QColor ThemePalette::buttonBackground() const { return accent_; }
QColor ThemePalette::buttonHover() const { return accentHover_; }
QColor ThemePalette::buttonPressed() const { return accentPressed_; }

QColor ThemePalette::accent() const { return accent_; }
QColor ThemePalette::accentHover() const { return accentHover_; }
QColor ThemePalette::accentHoverText() const { return accentHoverText_; }
QColor ThemePalette::accentPressed() const { return accentPressed_; }
QColor ThemePalette::accentPressedText() const { return accentPressedText_; }
QColor ThemePalette::accentSubtle() const { return accentSubtle_; }
QColor ThemePalette::background() const { return background_; }
QColor ThemePalette::surface() const { return surface_; }
QColor ThemePalette::surfaceSecondary() const { return surfaceSecondary_; }
QColor ThemePalette::control() const { return control_; }
QColor ThemePalette::controlHover() const { return controlHover_; }
QColor ThemePalette::border() const { return border_; }
QColor ThemePalette::textPrimary() const { return textPrimary_; }
QColor ThemePalette::textSecondary() const { return textSecondary_; }
QColor ThemePalette::disabledText() const { return disabledText_; }
QColor ThemePalette::selection() const { return selection_; }
QColor ThemePalette::selectionText() const { return selectionText_; }
QColor ThemePalette::accentText() const { return accentText_; }
QColor ThemePalette::focus() const { return focus_; }
QColor ThemePalette::error() const { return error_; }
bool ThemePalette::highContrast() const { return highContrast_; }

bool ThemePalette::isValid() const
{
    return background_.isValid()
        && surface_.isValid()
        && textPrimary_.isValid()
        && accent_.isValid();
}

bool ThemePalette::operator==(const ThemePalette &other) const
{
    return mode_ == other.mode_
        && highContrast_ == other.highContrast_
        && accent_ == other.accent_
        && accentHover_ == other.accentHover_
        && accentHoverText_ == other.accentHoverText_
        && accentPressed_ == other.accentPressed_
        && accentPressedText_ == other.accentPressedText_
        && accentSubtle_ == other.accentSubtle_
        && background_ == other.background_
        && surface_ == other.surface_
        && surfaceSecondary_ == other.surfaceSecondary_
        && control_ == other.control_
        && controlHover_ == other.controlHover_
        && border_ == other.border_
        && textPrimary_ == other.textPrimary_
        && textSecondary_ == other.textSecondary_
        && disabledText_ == other.disabledText_
        && selection_ == other.selection_
        && selectionText_ == other.selectionText_
        && accentText_ == other.accentText_
        && focus_ == other.focus_
        && error_ == other.error_;
}

bool ThemePalette::operator!=(const ThemePalette &other) const
{
    return !(*this == other);
}

} // namespace FilePilot
