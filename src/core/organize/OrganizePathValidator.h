#pragma once

#include <QString>

namespace FilePilot {

enum class PathValidationCode {
    Valid,
    Empty,
    DotName,
    DotDotName,
    InvalidCharacter,
    ReservedName,
    TrailingSpaceOrDot
};

struct PathValidationResult {
    bool valid = false;
    PathValidationCode code = PathValidationCode::Valid;
    QString message;
};

class OrganizePathValidator
{
public:
    static PathValidationResult validateCategoryName(const QString &category);
    static PathValidationResult validateFileName(const QString &fileName);
    static QString normalizePath(const QString &path);
    static QString destinationPath(const QString &root,
                                   const QString &category,
                                   const QString &fileName);
    static bool isPathInsideRoot(const QString &root, const QString &destination);
};

} // namespace FilePilot
