#pragma once

class QString;

namespace FilePilot {

// These are deliberately separate from the advanced FileOperator: v1 only
// needs direct copy/move/remove operations with bool + errorMessage results.
namespace BasicFileOperations {

bool copy(const QString &sourcePath,
          const QString &targetDirectory,
          QString &errorMessage);
bool move(const QString &sourcePath,
          const QString &targetDirectory,
          QString &errorMessage);
bool remove(const QString &filePath, QString &errorMessage);

} // namespace BasicFileOperations
} // namespace FilePilot