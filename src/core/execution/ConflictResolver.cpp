#include "core/execution/ConflictResolver.h"

#include "core/organize/OrganizePathValidator.h"

#include <QFileInfo>

#include <filesystem>

namespace FilePilot {

namespace {

namespace fs = std::filesystem;

QString normalizedKey(const QString &path)
{
    return OrganizePathValidator::normalizePath(path).toLower();
}

bool destinationExists(const QString &path)
{
    std::error_code error;
    return fs::exists(
        fs::u8path(path.toStdString()), error) && !error;
}

} // namespace

std::vector<ConflictDecision> ConflictResolver::resolve(
    const std::vector<OrganizePlanItem> &items,
    const ConflictPolicy policy) const
{
    std::vector<ConflictDecision> decisions;
    QSet<QString> reservedDestinations;

    for (const OrganizePlanItem &item : items) {
        ConflictDecision decision = resolveSingle(
            item, policy, reservedDestinations);
        if (decision.action != ConflictDecisionAction::Reject) {
            reservedDestinations.insert(
                normalizedKey(decision.destinationPath));
        }
        decisions.push_back(std::move(decision));
    }

    return decisions;
}

ConflictDecision ConflictResolver::resolveSingle(
    const OrganizePlanItem &item,
    const ConflictPolicy policy,
    const QSet<QString> &reservedDestinations) const
{
    ConflictDecision decision;
    decision.item = item;
    decision.destinationPath = item.destinationPath;

    const QString normalizedDestination =
        normalizedKey(item.destinationPath);
    if (reservedDestinations.contains(normalizedDestination)) {
        decision.action = ConflictDecisionAction::Reject;
        decision.planInternalConflict = true;
        decision.errorMessage = QStringLiteral("计划内部目标路径冲突");
        return decision;
    }

    if (!destinationExists(item.destinationPath)) {
        decision.action = ConflictDecisionAction::Proceed;
        return decision;
    }

    switch (policy) {
    case ConflictPolicy::Skip:
        decision.action = ConflictDecisionAction::Skip;
        decision.errorMessage = QStringLiteral("目标文件已存在");
        return decision;
    case ConflictPolicy::Overwrite:
        decision.action = ConflictDecisionAction::Overwrite;
        return decision;
    case ConflictPolicy::AutoRename: {
        const QString renamed =
            nextAvailableDestination(item, reservedDestinations);
        if (renamed.isEmpty()) {
            decision.action = ConflictDecisionAction::Reject;
            decision.errorMessage =
                QStringLiteral("无法生成安全的自动重命名目标路径");
            return decision;
        }

        decision.action = ConflictDecisionAction::AutoRename;
        decision.destinationPath = renamed;
        return decision;
    }
    }

    decision.action = ConflictDecisionAction::Reject;
    decision.errorMessage = QStringLiteral("未知冲突策略");
    return decision;
}

QString ConflictResolver::nextAvailableDestination(
    const OrganizePlanItem &item,
    const QSet<QString> &reservedDestinations) const
{
    const QFileInfo destinationInfo(item.destinationPath);
    const QString directory = destinationInfo.absolutePath();
    const QString suffix = destinationInfo.completeSuffix();
    const QString baseName = suffix.isEmpty()
        ? destinationInfo.completeBaseName()
        : destinationInfo.completeBaseName();

    for (int index = 1; index <= 9999; ++index) {
        QString candidateName =
            QStringLiteral("%1(%2)").arg(baseName).arg(index);
        if (!suffix.isEmpty()) {
            candidateName += QLatin1Char('.') + suffix;
        }

        if (candidateName.size() > 255) {
            return {};
        }

        const PathValidationResult nameValidation =
            OrganizePathValidator::validateFileName(candidateName);
        if (!nameValidation.valid) {
            continue;
        }

        const QString candidate =
            OrganizePathValidator::destinationPath(
                directory,
                QString(),
                candidateName);
        if (!OrganizePathValidator::isPathInsideRoot(directory, candidate)) {
            continue;
        }
        if (reservedDestinations.contains(normalizedKey(candidate))
            || destinationExists(candidate)) {
            continue;
        }

        return candidate;
    }

    return {};
}

} // namespace FilePilot
