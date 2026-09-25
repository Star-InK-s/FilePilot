#include "core/organize/OrganizePathValidator.h"

#include <QDir>
#include <QSet>

namespace FilePilot {

namespace {

const QSet<QString> &reservedNames()
{
    static const QSet<QString> names = {
        QStringLiteral("CON"),
        QStringLiteral("PRN"),
        QStringLiteral("AUX"),
        QStringLiteral("NUL"),
        QStringLiteral("COM1"),
        QStringLiteral("COM2"),
        QStringLiteral("COM3"),
        QStringLiteral("COM4"),
        QStringLiteral("COM5"),
        QStringLiteral("COM6"),
        QStringLiteral("COM7"),
        QStringLiteral("COM8"),
        QStringLiteral("COM9"),
        QStringLiteral("LPT1"),
        QStringLiteral("LPT2"),
        QStringLiteral("LPT3"),
        QStringLiteral("LPT4"),
        QStringLiteral("LPT5"),
        QStringLiteral("LPT6"),
        QStringLiteral("LPT7"),
        QStringLiteral("LPT8"),
        QStringLiteral("LPT9"),
    };
    return names;
}

PathValidationResult invalid(const PathValidationCode code, const QString &message)
{
    return PathValidationResult{false, code, message};
}

PathValidationResult validateLeafName(const QString &name,
                                      const QString &label,
                                      const bool allowWhitespaceOnly)
{
    if (name.isEmpty() || (!allowWhitespaceOnly && name.trimmed().isEmpty())) {
        return invalid(
            PathValidationCode::Empty,
            QStringLiteral("%1 不能为空").arg(label));
    }

    if (name == QStringLiteral(".")) {
        return invalid(
            PathValidationCode::DotName,
            QStringLiteral("%1 不能是 .").arg(label));
    }

    if (name == QStringLiteral("..")) {
        return invalid(
            PathValidationCode::DotDotName,
            QStringLiteral("%1 不能是 ..").arg(label));
    }

    static const QString invalidCharacters = QStringLiteral("<>:\"/\\|?*");
    for (const QChar character : name) {
        if (character.unicode() < 32
            || invalidCharacters.contains(character)) {
            return invalid(
                PathValidationCode::InvalidCharacter,
                QStringLiteral("%1 包含 Windows 非法字符").arg(label));
        }
    }

    const QString baseName = name.section(QLatin1Char('.'), 0, 0).toUpper();
    if (reservedNames().contains(baseName)) {
        return invalid(
            PathValidationCode::ReservedName,
            QStringLiteral("%1 使用了 Windows 保留名称").arg(label));
    }

    if (name.endsWith(QLatin1Char(' ')) || name.endsWith(QLatin1Char('.'))) {
        return invalid(
            PathValidationCode::TrailingSpaceOrDot,
            QStringLiteral("%1 不能以空格或点号结尾").arg(label));
    }

    return PathValidationResult{true, PathValidationCode::Valid, QString()};
}

} // namespace

PathValidationResult OrganizePathValidator::validateCategoryName(
    const QString &category)
{
    return validateLeafName(category, QStringLiteral("分类"), false);
}

PathValidationResult OrganizePathValidator::validateFileName(
    const QString &fileName)
{
    return validateLeafName(fileName, QStringLiteral("文件名"), false);
}

QString OrganizePathValidator::normalizePath(const QString &path)
{
    QString normalized = path;
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return QDir::cleanPath(normalized);
}

QString OrganizePathValidator::destinationPath(
    const QString &root,
    const QString &category,
    const QString &fileName)
{
    return normalizePath(
        normalizePath(root) + QLatin1Char('/') + category + QLatin1Char('/') + fileName);
}

bool OrganizePathValidator::isPathInsideRoot(
    const QString &root,
    const QString &destination)
{
    const QString normalizedRoot = normalizePath(root);
    const QString normalizedDestination = normalizePath(destination);
    if (normalizedRoot.isEmpty() || normalizedDestination.isEmpty()
        || normalizedRoot == normalizedDestination) {
        return false;
    }

    QString prefix = normalizedRoot;
    if (!prefix.endsWith(QLatin1Char('/'))) {
        prefix += QLatin1Char('/');
    }

    return normalizedDestination.startsWith(prefix, Qt::CaseInsensitive);
}

bool OrganizePathValidator::pathsEqual(
    const QString &left,
    const QString &right)
{
    return normalizePath(left).compare(
        normalizePath(right), Qt::CaseInsensitive) == 0;
}

TargetRootInfo OrganizePathValidator::inspectTargetRoot(
    const QString &targetRoot)
{
    TargetRootInfo info;
    if (targetRoot.trimmed().isEmpty()) {
        info.kind = TargetRootKind::Empty;
        info.message = QStringLiteral("目标根目录不能为空");
        return info;
    }

    QString rawPath = targetRoot;
    rawPath.replace(QLatin1Char(92), QLatin1Char('/'));
    info.normalizedPath = normalizePath(targetRoot);
    if (rawPath.startsWith(QStringLiteral("//./"))
        || rawPath.startsWith(QStringLiteral("//?/"))) {
        info.kind = TargetRootKind::DeviceNamespace;
    } else if (rawPath.startsWith(QStringLiteral("//"))) {
        info.kind = TargetRootKind::Unc;
    } else if (QDir::isAbsolutePath(info.normalizedPath)) {
        info.kind = TargetRootKind::Absolute;
    } else {
        info.kind = TargetRootKind::Relative;
    }

    info.valid = true;
    return info;
}

} // namespace FilePilot
