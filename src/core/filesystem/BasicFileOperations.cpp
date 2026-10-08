#include "core/filesystem/BasicFileOperations.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

namespace FilePilot {
namespace BasicFileOperations {

namespace {

bool validateSource(const QString &sourcePath, QString &errorMessage)
{
    const QFileInfo source(sourcePath);
    if (!source.exists() || !source.isFile()) {
        errorMessage = QStringLiteral("文件不存在：%1").arg(sourcePath);
        return false;
    }
    return true;
}

bool validateTargetDirectory(const QString &targetDirectory, QString &errorMessage)
{
    const QFileInfo target(targetDirectory);
    if (!target.exists() || !target.isDir()) {
        errorMessage = QStringLiteral("目标路径不存在：%1").arg(targetDirectory);
        return false;
    }
    return true;
}

QString destinationPath(const QString &sourcePath, const QString &targetDirectory)
{
    return QDir(targetDirectory).filePath(QFileInfo(sourcePath).fileName());
}

bool validateDestination(const QString &destinationPath, QString &errorMessage)
{
    if (QFileInfo::exists(destinationPath)) {
        errorMessage = QStringLiteral("目标文件已存在：%1").arg(destinationPath);
        return false;
    }
    return true;
}

QString fileError(const QString &action, const QFile &file)
{
    if (file.error() == QFileDevice::PermissionsError) {
        return QStringLiteral("权限不足：%1").arg(file.errorString());
    }
    return QStringLiteral("%1失败：%2").arg(action, file.errorString());
}

} // namespace

bool copy(const QString &sourcePath,
          const QString &targetDirectory,
          QString &errorMessage)
{
    errorMessage.clear();
    if (!validateSource(sourcePath, errorMessage)
        || !validateTargetDirectory(targetDirectory, errorMessage)) {
        return false;
    }

    const QString destination = destinationPath(sourcePath, targetDirectory);
    if (!validateDestination(destination, errorMessage)) {
        return false;
    }

    // QFile::copy is enough for v1; the source remains untouched.
    QFile sourceFile(sourcePath);
    if (!sourceFile.copy(destination)) {
        errorMessage = fileError(QStringLiteral("复制"), sourceFile);
        return false;
    }
    return true;
}

bool move(const QString &sourcePath,
          const QString &targetDirectory,
          QString &errorMessage)
{
    errorMessage.clear();
    if (!validateSource(sourcePath, errorMessage)
        || !validateTargetDirectory(targetDirectory, errorMessage)) {
        return false;
    }

    const QString destination = destinationPath(sourcePath, targetDirectory);
    if (!validateDestination(destination, errorMessage)) {
        return false;
    }

    // QFile::rename is the simple v1 move. Cross-volume fallback is outside
    // the current scope.
    QFile sourceFile(sourcePath);
    if (!sourceFile.rename(destination)) {
        errorMessage = fileError(QStringLiteral("移动"), sourceFile);
        return false;
    }
    return true;
}

bool remove(const QString &filePath, QString &errorMessage)
{
    errorMessage.clear();
    if (!validateSource(filePath, errorMessage)) {
        return false;
    }

    // v1 has no undo or recovery, so removal remains a direct Qt operation.
    QFile file(filePath);
    if (!file.remove()) {
        errorMessage = fileError(QStringLiteral("删除"), file);
        return false;
    }
    return true;
}

} // namespace BasicFileOperations
} // namespace FilePilot