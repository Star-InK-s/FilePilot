#pragma once

#include "core/classify/ClassificationRule.h"
#include "core/model/FileInfo.h"
#include "core/scan/ScanService.h"

#include <QString>

#include <vector>

namespace FilePilot {

class RuleEngine
{
public:
    RuleEngine();
    explicit RuleEngine(std::vector<ClassificationRule> rules);

    const std::vector<ClassificationRule> &rules() const;

    QString classify(const FileInfo &file) const;
    void classify(ScanResult &result) const;

    static QString normalizeExtension(const QString &extension);

private:
    std::vector<ClassificationRule> rules_;
};

} // namespace FilePilot
