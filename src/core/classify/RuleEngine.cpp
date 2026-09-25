#include "core/classify/RuleEngine.h"

#include <algorithm>
#include <utility>

namespace FilePilot {

namespace {

bool ruleMatches(const ClassificationRule &rule, const QString &extension)
{
    return rule.enabled
        && (rule.matchesAnyExtension || rule.extensions.contains(extension));
}

} // namespace

RuleEngine::RuleEngine()
    : RuleEngine(defaultClassificationRules())
{
}

RuleEngine::RuleEngine(std::vector<ClassificationRule> rules)
    : rules_(std::move(rules))
{
    for (ClassificationRule &rule : rules_) {
        for (QString &extension : rule.extensions) {
            extension = normalizeExtension(extension);
        }
    }

    std::stable_sort(
        rules_.begin(),
        rules_.end(),
        [](const ClassificationRule &left, const ClassificationRule &right) {
            return left.priority < right.priority;
        });
}

const std::vector<ClassificationRule> &RuleEngine::rules() const
{
    return rules_;
}

QString RuleEngine::classify(const FileInfo &file) const
{
    const QString extension = normalizeExtension(file.extension);
    for (const ClassificationRule &rule : rules_) {
        if (ruleMatches(rule, extension)) {
            return rule.category;
        }
    }

    return QStringLiteral("Others");
}

void RuleEngine::classify(ScanResult &result) const
{
    for (FileInfo &file : result.files) {
        file.category = classify(file);
    }
}

QString RuleEngine::normalizeExtension(const QString &extension)
{
    QString normalized = extension.trimmed().toLower();
    while (normalized.startsWith(QLatin1Char('.'))) {
        normalized.remove(0, 1);
    }

    return normalized;
}

} // namespace FilePilot
