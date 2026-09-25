#include "core/scan/ScanService.h"

#include <QDateTime>
#include <QFileInfo>
#include <QTimeZone>

#include <chrono>
#include <filesystem>
#include <limits>
#include <system_error>
#include <utility>

namespace FilePilot {

namespace {

namespace fs = std::filesystem;

QString pathToString(const fs::path &path)
{
    return QString::fromStdString(path.u8string());
}

fs::path pathFromString(const QString &path)
{
    return fs::u8path(path.toStdString());
}

QString errorMessage(const std::error_code &error)
{
    const std::string message = error.message();
    return message.empty()
        ? QStringLiteral("文件系统操作失败")
        : QString::fromLocal8Bit(message.c_str());
}

QDateTime fileTimeToUtc(const fs::file_time_type &value)
{
    using namespace std::chrono;

    const auto systemNow = system_clock::now();
    const auto fileNow = fs::file_time_type::clock::now();
    const auto systemValue =
        time_point_cast<system_clock::duration>(value - fileNow + systemNow);

    const auto millisecondsSinceEpoch =
        duration_cast<milliseconds>(systemValue.time_since_epoch());
    return QDateTime::fromMSecsSinceEpoch(
        static_cast<qint64>(millisecondsSinceEpoch.count()), QTimeZone::UTC);
}

QString extensionKey(const fs::path &path)
{
    const QString extension =
        QString::fromStdString(path.extension().u8string());
    return extension.isEmpty()
        ? QStringLiteral("(none)")
        : extension.mid(1).toLower();
}

bool checkedSize(const uintmax_t size, qint64 &result)
{
    if (size > static_cast<uintmax_t>(std::numeric_limits<qint64>::max())) {
        return false;
    }

    result = static_cast<qint64>(size);
    return true;
}

ScanError makeScanError(const fs::path &path,
                        const std::error_code &error)
{
    return ScanError{
        pathToString(path),
        errorMessage(error),
        error.value(),
    };
}

void appendError(ScanResult &result,
                 const ScanError &error,
                 const ScanErrorCallback &errorCallback)
{
    result.errors.push_back(error);
    ++result.statistics.errorCount;

    if (errorCallback) {
        errorCallback(error);
    }
}

void reportProgress(const ScanResult &result,
                    const QString &currentDirectory,
                    const QString &currentFile,
                    const ScanProgressCallback &progressCallback)
{
    if (!progressCallback) {
        return;
    }

    progressCallback(ScanProgress{
        result.statistics.fileCount,
        result.statistics.errorCount,
        currentDirectory,
        currentFile,
    });
}

} // namespace

ScanCancellationToken::ScanCancellationToken()
    : cancelled_(std::make_shared<std::atomic_bool>(false))
{
}

void ScanCancellationToken::cancel() noexcept
{
    cancelled_->store(true, std::memory_order_relaxed);
}

bool ScanCancellationToken::isCancelled() const noexcept
{
    return cancelled_->load(std::memory_order_relaxed);
}

ScanResult ScanService::scan(const QString &rootPath) const
{
    ScanCancellationToken cancellationToken;
    return scan(rootPath, cancellationToken);
}

ScanResult ScanService::scan(
    const QString &rootPath,
    const ScanCancellationToken &cancellationToken,
    const ScanProgressCallback &progressCallback,
    const ScanErrorCallback &errorCallback) const
{
    ScanResult result;

    if (cancellationToken.isCancelled()) {
        result.cancelled = true;
        return result;
    }

    if (rootPath.trimmed().isEmpty()) {
        result.fatalError = QStringLiteral("扫描目录不能为空");
        result.errors.push_back(ScanError{
            rootPath,
            result.fatalError,
            static_cast<int>(std::errc::invalid_argument),
        });
        result.statistics.errorCount = 1;
        return result;
    }

    std::error_code error;
    const fs::path requestedRoot = pathFromString(rootPath);
    const fs::path root = fs::absolute(requestedRoot, error);
    if (error) {
        result.fatalError =
            QStringLiteral("无法解析扫描目录：%1").arg(errorMessage(error));
        appendError(result, makeScanError(requestedRoot, error), errorCallback);
        return result;
    }

    result.rootPath = pathToString(root);

    const bool rootIsDirectory = fs::is_directory(root, error);
    if (error) {
        result.fatalError =
            QStringLiteral("无法访问扫描目录：%1").arg(errorMessage(error));
        appendError(result, makeScanError(root, error), errorCallback);
        return result;
    }

    if (!rootIsDirectory) {
        result.fatalError = QStringLiteral("扫描路径不存在或不是目录");
        appendError(
            result,
            ScanError{
                result.rootPath,
                result.fatalError,
                static_cast<int>(std::errc::not_a_directory),
            },
            errorCallback);
        return result;
    }

    fs::recursive_directory_iterator iterator(
        root,
        fs::directory_options::skip_permission_denied,
        error);
    const fs::recursive_directory_iterator end;

    if (error) {
        result.fatalError =
            QStringLiteral("无法开始目录扫描：%1").arg(errorMessage(error));
        appendError(result, makeScanError(root, error), errorCallback);
        return result;
    }

    QString currentDirectory = result.rootPath;

    while (iterator != end) {
        if (cancellationToken.isCancelled()) {
            result.cancelled = true;
            reportProgress(
                result, currentDirectory, QString(), progressCallback);
            return result;
        }

        const fs::directory_entry entry = *iterator;
        std::error_code entryError;
        const fs::file_status linkStatus = entry.symlink_status(entryError);

        if (entryError) {
            appendError(
                result, makeScanError(entry.path(), entryError), errorCallback);
            reportProgress(
                result,
                currentDirectory,
                pathToString(entry.path()),
                progressCallback);
        } else if (fs::is_symlink(linkStatus)) {
            // Links are never followed. This prevents cycles and repeated scans.
        } else if (fs::is_directory(linkStatus)) {
            currentDirectory = pathToString(entry.path());

            std::error_code accessError;
            const fs::directory_iterator probe(
                entry.path(),
                fs::directory_options::none,
                accessError);
            if (accessError) {
                appendError(
                    result,
                    makeScanError(entry.path(), accessError),
                    errorCallback);
            }
        } else if (fs::is_regular_file(linkStatus)) {
            const fs::path &filePath = entry.path();
            std::error_code sizeError;
            const uintmax_t rawSize = fs::file_size(filePath, sizeError);

            if (sizeError) {
                appendError(
                    result, makeScanError(filePath, sizeError), errorCallback);
                reportProgress(
                    result,
                    currentDirectory,
                    pathToString(filePath),
                    progressCallback);
            } else {
                qint64 fileSize = 0;
                if (!checkedSize(rawSize, fileSize)) {
                    appendError(
                        result,
                        ScanError{
                            pathToString(filePath),
                            QStringLiteral("文件大小超出支持范围"),
                            static_cast<int>(std::errc::value_too_large),
                        },
                        errorCallback);
                } else {
                    FileInfo fileInfo;
                    fileInfo.absolutePath = pathToString(filePath);
                    fileInfo.fileName =
                        QString::fromStdString(filePath.filename().u8string());
                    fileInfo.extension =
                        QString::fromStdString(filePath.extension().u8string())
                            .mid(1)
                            .toLower();
                    fileInfo.sizeBytes = fileSize;
                    fileInfo.kind = FileKind::RegularFile;

                    std::error_code timeError;
                    const fs::file_time_type modifiedTime =
                        fs::last_write_time(filePath, timeError);
                    if (!timeError) {
                        fileInfo.modifiedUtc = fileTimeToUtc(modifiedTime);
                    } else {
                        appendError(
                            result,
                            makeScanError(filePath, timeError),
                            errorCallback);
                    }

                    const QFileInfo qtFileInfo(pathToString(filePath));
                    fileInfo.createdUtc = qtFileInfo.birthTime().toUTC();
                    if (!fileInfo.modifiedUtc.isValid()) {
                        fileInfo.modifiedUtc =
                            qtFileInfo.lastModified().toUTC();
                    }

                    result.files.push_back(std::move(fileInfo));
                    ++result.statistics.fileCount;
                    result.statistics.totalSizeBytes += fileSize;
                    ++result.statistics.extensionCounts[extensionKey(filePath)];
                }

                reportProgress(
                    result,
                    currentDirectory,
                    pathToString(filePath),
                    progressCallback);
            }
        }

        iterator.increment(error);
        if (error) {
            result.fatalError =
                QStringLiteral("目录遍历中断：%1").arg(errorMessage(error));
            appendError(
                result, makeScanError(entry.path(), error), errorCallback);
            return result;
        }
    }

    result.completed = true;
    reportProgress(result, currentDirectory, QString(), progressCallback);
    return result;
}

} // namespace FilePilot

