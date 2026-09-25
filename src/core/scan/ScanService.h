#pragma once

#include "core/model/FileInfo.h"

#include <QHash>
#include <QList>
#include <QMetaType>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace FilePilot {

class ScanCancellationToken
{
public:
    ScanCancellationToken();

    void cancel() noexcept;
    bool isCancelled() const noexcept;

private:
    std::shared_ptr<std::atomic_bool> cancelled_;
};

struct ScanError {
    QString path;
    QString message;
    int code = 0;
};

struct ScanProgress {
    qint64 scannedFileCount = 0;
    qint64 errorCount = 0;
    QString currentDirectory;
    QString currentFile;
};

struct ScanErrorBatch {
    qint64 totalErrorCount = 0;
    QList<ScanError> errors;
};

struct ScanStatistics {
    qint64 fileCount = 0;
    qint64 totalSizeBytes = 0;
    qint64 errorCount = 0;
    QHash<QString, qint64> extensionCounts;
};

struct ScanResult {
    QString rootPath;
    std::vector<FileInfo> files;
    ScanStatistics statistics;
    std::vector<ScanError> errors;
    bool completed = false;
    bool cancelled = false;
    QString fatalError;
};

using ScanProgressCallback = std::function<void(const ScanProgress &)>;
using ScanErrorCallback = std::function<void(const ScanError &)>;

class ScanService
{
public:
    ScanResult scan(const QString &rootPath) const;

    ScanResult scan(
        const QString &rootPath,
        const ScanCancellationToken &cancellationToken,
        const ScanProgressCallback &progressCallback = {},
        const ScanErrorCallback &errorCallback = {}) const;
};

} // namespace FilePilot

Q_DECLARE_METATYPE(FilePilot::ScanError)
Q_DECLARE_METATYPE(FilePilot::ScanProgress)
Q_DECLARE_METATYPE(FilePilot::ScanErrorBatch)
Q_DECLARE_METATYPE(FilePilot::ScanResult)
