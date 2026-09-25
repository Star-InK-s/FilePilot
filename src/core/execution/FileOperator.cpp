#include "core/execution/FileOperator.h"

#include "core/organize/OrganizePathValidator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

#include <filesystem>
#include <limits>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace FilePilot {

namespace {

namespace fs = std::filesystem;

QString temporaryDestinationPath(const QString &destination)
{
    return destination + QStringLiteral(".filepilot-part-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool ensureParentDirectory(const QString &destination, QString &error)
{
    const QFileInfo destinationInfo(destination);
    std::error_code code;
    fs::create_directories(
        fs::u8path(destinationInfo.absolutePath().toStdString()), code);
    if (code) {
        error = QStringLiteral("无法创建目标目录");
        return false;
    }

    return true;
}

bool moveWithoutReplacement(const QString &source, const QString &destination)
{
#ifdef Q_OS_WIN
    return MoveFileExW(
        reinterpret_cast<LPCWSTR>(source.utf16()),
        reinterpret_cast<LPCWSTR>(destination.utf16()),
        MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    std::error_code code;
    fs::rename(fs::u8path(source.toStdString()), fs::u8path(destination.toStdString()), code);
    return !code;
#endif
}

bool replaceExisting(const QString &source, const QString &destination)
{
#ifdef Q_OS_WIN
    return ReplaceFileW(
        reinterpret_cast<LPCWSTR>(destination.utf16()),
        reinterpret_cast<LPCWSTR>(source.utf16()),
        nullptr,
        REPLACEFILE_WRITE_THROUGH,
        nullptr,
        nullptr) != FALSE;
#else
    std::error_code code;
    fs::rename(fs::u8path(source.toStdString()), fs::u8path(destination.toStdString()), code);
    return !code;
#endif
}

bool sameVolume(const QString &left, const QString &right)
{
#ifdef Q_OS_WIN
    wchar_t leftRoot[MAX_PATH]{};
    wchar_t rightRoot[MAX_PATH]{};
    if (GetVolumePathNameW(
            reinterpret_cast<LPCWSTR>(left.utf16()), leftRoot, MAX_PATH) == FALSE
        || GetVolumePathNameW(
            reinterpret_cast<LPCWSTR>(right.utf16()), rightRoot, MAX_PATH) == FALSE) {
        return false;
    }

    DWORD leftSerial = 0;
    DWORD rightSerial = 0;
    if (GetVolumeInformationW(
            leftRoot, nullptr, 0, &leftSerial, nullptr, nullptr, nullptr, 0) == FALSE
        || GetVolumeInformationW(
            rightRoot, nullptr, 0, &rightSerial, nullptr, nullptr, nullptr, 0) == FALSE) {
        return false;
    }

    return leftSerial == rightSerial;
#else
    std::error_code code;
    return fs::u8path(left.toStdString()).root_path()
        == fs::u8path(right.toStdString()).root_path();
#endif
}

bool copyToTemporary(
    const QString &source,
    const QString &temporary,
    const std::atomic_bool &cancelled,
    QString &error)
{
    QFile sourceFile(source);
    if (!sourceFile.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("无法读取源文件");
        return false;
    }

    QFile temporaryFile(temporary);
    if (!temporaryFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        error = QStringLiteral("无法写入临时目标文件");
        return false;
    }

    constexpr int bufferSize = 1024 * 1024;
    QByteArray buffer(bufferSize, Qt::Uninitialized);
    qint64 totalWritten = 0;
    while (!sourceFile.atEnd()) {
        if (cancelled.load(std::memory_order_relaxed)) {
            error = QStringLiteral("操作已取消");
            return false;
        }

        const qint64 bytesRead = sourceFile.read(buffer.data(), buffer.size());
        if (bytesRead < 0) {
            error = QStringLiteral("读取源文件失败");
            return false;
        }
        if (bytesRead == 0) {
            break;
        }

        const qint64 bytesWritten =
            temporaryFile.write(buffer.constData(), bytesRead);
        if (bytesWritten != bytesRead) {
            error = QStringLiteral("写入临时目标文件失败");
            return false;
        }
        totalWritten += bytesWritten;
    }

    temporaryFile.flush();
    if (totalWritten != QFileInfo(source).size()) {
        error = QStringLiteral("复制完整性校验失败");
        return false;
    }

    return true;
}

} // namespace

FileMoveResult FileOperator::move(
    const FileMoveRequest &request,
    const std::atomic_bool &cancelled) const
{
    FileMoveResult result;
    result.actualDestination = request.destinationPath;

    if (request.action == ConflictDecisionAction::Skip) {
        result.status = ExecutionItemStatus::Skipped;
        result.errorMessage = QStringLiteral("目标文件已存在");
        return result;
    }
    if (request.action == ConflictDecisionAction::Reject) {
        result.status = ExecutionItemStatus::Rejected;
        result.errorMessage = QStringLiteral("计划项已被拒绝");
        return result;
    }
    if (cancelled.load(std::memory_order_relaxed)) {
        result.status = ExecutionItemStatus::Cancelled;
        result.errorMessage = QStringLiteral("操作已取消");
        return result;
    }

    const QFileInfo sourceInfo(request.sourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        result.status = ExecutionItemStatus::Rejected;
        result.errorMessage = QStringLiteral("源文件不存在或不是普通文件");
        return result;
    }
    if (OrganizePathValidator::pathsEqual(
            request.sourcePath, request.destinationPath)) {
        result.status = ExecutionItemStatus::Rejected;
        result.errorMessage = QStringLiteral("源路径与目标路径相同");
        return result;
    }

    QString directoryError;
    if (!ensureParentDirectory(request.destinationPath, directoryError)) {
        result.status = ExecutionItemStatus::Failed;
        result.errorMessage = directoryError;
        return result;
    }

    const bool overwrite =
        request.action == ConflictDecisionAction::Overwrite;
    const bool destinationExists =
        QFileInfo::exists(request.destinationPath);

    if (sameVolume(request.sourcePath, request.destinationPath)) {
        const bool moved = overwrite && destinationExists
            ? replaceExisting(request.sourcePath, request.destinationPath)
            : moveWithoutReplacement(request.sourcePath, request.destinationPath);
        if (!moved) {
            result.status = ExecutionItemStatus::Failed;
            result.errorMessage = QStringLiteral("移动文件失败");
            return result;
        }

        result.status = ExecutionItemStatus::Succeeded;
        return result;
    }

    const QString temporaryPath =
        temporaryDestinationPath(request.destinationPath);
    QString copyError;
    if (!copyToTemporary(
            request.sourcePath, temporaryPath, cancelled, copyError)) {
        QFile::remove(temporaryPath);
        result.status = cancelled.load(std::memory_order_relaxed)
            ? ExecutionItemStatus::Cancelled
            : ExecutionItemStatus::Failed;
        result.errorMessage = copyError;
        return result;
    }

    const qint64 sourceSize = QFileInfo(request.sourcePath).size();
    const bool published = overwrite && destinationExists
        ? replaceExisting(temporaryPath, request.destinationPath)
        : moveWithoutReplacement(temporaryPath, request.destinationPath);
    if (!published) {
        QFile::remove(temporaryPath);
        result.status = ExecutionItemStatus::Failed;
        result.errorMessage = QStringLiteral("目标文件写入失败");
        return result;
    }

    if (QFileInfo(request.destinationPath).size() != sourceSize) {
        result.status = ExecutionItemStatus::Failed;
        result.errorMessage = QStringLiteral("目标文件完整性校验失败");
        return result;
    }

    std::error_code removeError;
    fs::remove(fs::u8path(request.sourcePath.toStdString()), removeError);
    if (removeError) {
        result.status = ExecutionItemStatus::SourceCleanupFailed;
        result.errorMessage = QStringLiteral("目标文件已写入，但源文件删除失败");
        return result;
    }

    result.status = ExecutionItemStatus::Succeeded;
    return result;
}

} // namespace FilePilot
