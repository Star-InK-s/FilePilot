#pragma once

#include <QString>
#include <QStringList>

#include <vector>

namespace FilePilot {

struct ClassificationRule {
    QString name;
    int priority = 1000;
    bool enabled = true;
    QString category;
    QStringList extensions;
    bool matchesAnyExtension = false;
};

std::vector<ClassificationRule> defaultClassificationRules();

} // namespace FilePilot
