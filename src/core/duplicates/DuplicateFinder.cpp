#include "core/duplicates/DuplicateFinder.h"

#include "core/filesystem/FileIdentity.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <algorithm>
#include <map>
#include <optional>
#include <vector>

namespace FilePilot {

namespace {

constexpr int sampleSize = 4096;
constexpr int hashBufferSize = 1024 * 1024;

struct Candidate {
    FileInfo file;
    DuplicateItem item;
    FileIdentity identity;
};

struct HashOutput {
    bool ok = false;
    bool cancelled = false;
    bool changed = false;
    QByteArray digest;
    FileIdentity identity;
    QString error;
};

bool inspectPath(
    const QString &path,
    FilesystemPathInfo &info,
    QString &error)
{
    return inspectFilesystemPath(path, info, error) && info.inspected;
}

bool sameFileState(
    const FilesystemPathInfo &before,
    const FilesystemPathInfo &after)
{
    return before.kind == after.kind
        && before.identity == after.identity
        && before.size == after.size
        && before.modifiedMSecs == after.modifiedMSecs;
}

bool matchesScannedFile(
    const FileInfo &file,
    const FilesystemPathInfo &info,
    QString &error)
{
    if (!info.isRegularFile() || !info.identity.valid) {
        error = QStringLiteral("文件不是可验证的普通文件");
        return false;
    }
    if (info.size != file.sizeBytes) {
        error = QStringLiteral("文件大小在扫描后发生变化");
        return false;
    }

    const QFileInfo qtInfo(file.absolutePath);
    if (!qtInfo.exists() || !qtInfo.isFile()) {
        error = QStringLiteral("文件在扫描后被删除");
        return false;
    }
    if (file.modifiedUtc.isValid()
        && qAbs(qtInfo.lastModified().toUTC().msecsTo(file.modifiedUtc)) > 1000) {
        error = QStringLiteral("文件修改时间在扫描后发生变化");
        return false;
    }
    return true;
}

HashOutput hashPartial(
    const Candidate &candidate,
    const ScanCancellationToken &cancellationToken)
{
    HashOutput output;
    if (cancellationToken.isCancelled()) {
        output.cancelled = true;
        return output;
    }

    FilesystemPathInfo before;
    QString error;
    if (!inspectPath(candidate.file.absolutePath, before, error)
        || !matchesScannedFile(candidate.file, before, error)) {
        output.error = error.isEmpty()
            ? QStringLiteral("无法读取文件进行部分内容哈希")
            : error;
        output.changed = error.contains(QStringLiteral("变化"))
            || error.contains(QStringLiteral("删除"));
        return output;
    }

    QFile file(candidate.file.absolutePath);
    if (!file.open(QIODevice::ReadOnly)) {
        output.error = QStringLiteral("无法读取文件进行部分内容哈希");
        return output;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArray::number(candidate.item.size));
    const qint64 size = candidate.item.size;
    if (size <= sampleSize * 3) {
        QByteArray buffer(hashBufferSize, Qt::Uninitialized);
        while (!file.atEnd()) {
            if (cancellationToken.isCancelled()) {
                output.cancelled = true;
                return output;
            }
            const qint64 bytesRead = file.read(buffer.data(), buffer.size());
            if (bytesRead < 0) {
                output.error = QStringLiteral("读取文件部分内容失败");
                return output;
            }
            hash.addData(QByteArrayView(buffer.constData(), bytesRead));
        }
    } else {
        const std::vector<qint64> offsets{
            0,
            size / 2 - sampleSize / 2,
            size - sampleSize,
        };
        for (const qint64 offset : offsets) {
            if (cancellationToken.isCancelled()) {
                output.cancelled = true;
                return output;
            }
            if (!file.seek(offset)) {
                output.error = QStringLiteral("定位文件部分内容失败");
                return output;
            }
            const QByteArray sample = file.read(sampleSize);
            if (sample.size() != sampleSize) {
                output.error = QStringLiteral("读取文件部分内容失败");
                return output;
            }
            hash.addData(sample);
        }
    }

    FilesystemPathInfo after;
    if (!inspectPath(candidate.file.absolutePath, after, error)
        || !sameFileState(before, after)) {
        output.changed = true;
        output.error = QStringLiteral("文件在哈希过程中发生变化");
        return output;
    }

    output.ok = true;
    output.identity = before.identity;
    output.digest = hash.result();
    return output;
}

HashOutput hashFull(
    const Candidate &candidate,
    const ScanCancellationToken &cancellationToken)
{
    HashOutput output;
    if (cancellationToken.isCancelled()) {
        output.cancelled = true;
        return output;
    }

    FilesystemPathInfo before;
    QString error;
    if (!inspectPath(candidate.file.absolutePath, before, error)
        || !matchesScannedFile(candidate.file, before, error)) {
        output.error = error.isEmpty()
            ? QStringLiteral("无法读取文件进行完整哈希")
            : error;
        output.changed = error.contains(QStringLiteral("变化"))
            || error.contains(QStringLiteral("删除"));
        return output;
    }

    QFile file(candidate.file.absolutePath);
    if (!file.open(QIODevice::ReadOnly)) {
        output.error = QStringLiteral("无法读取文件进行完整哈希");
        return output;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray buffer(hashBufferSize, Qt::Uninitialized);
    while (!file.atEnd()) {
        if (cancellationToken.isCancelled()) {
            output.cancelled = true;
            return output;
        }
        const qint64 bytesRead = file.read(buffer.data(), buffer.size());
        if (bytesRead < 0) {
            output.error = QStringLiteral("读取文件完整内容失败");
            return output;
        }
        hash.addData(QByteArrayView(buffer.constData(), bytesRead));
    }

    FilesystemPathInfo after;
    if (!inspectPath(candidate.file.absolutePath, after, error)
        || !sameFileState(before, after)) {
        output.changed = true;
        output.error = QStringLiteral("文件在完整哈希过程中发生变化");
        return output;
    }

    output.ok = true;
    output.identity = before.identity;
    output.digest = hash.result();
    return output;
}

void reportError(
    DuplicateResult &result,
    const QString &path,
    const QString &message,
    const DuplicateErrorCallback &errorCallback)
{
    result.errors.push_back(DuplicateError{path, message, 0});
    ++result.summary.errorCount;
    if (errorCallback) {
        errorCallback(result.errors.back());
    }
}

void reportProgress(
    const DuplicateResult &result,
    const DuplicatePhase phase,
    const QString &currentPath,
    const DuplicateProgressCallback &progressCallback)
{
    if (!progressCallback) {
        return;
    }
    progressCallback(DuplicateProgress{
        result.summary.processedFiles,
        result.summary.candidateFiles,
        currentPath,
        phase,
        result.summary.errorCount,
    });
}

void finalizeItem(
    DuplicateResult &result,
    Candidate &candidate,
    const DuplicateHashStatus status,
    const QString &errorMessage = {},
    const DuplicateErrorCallback &errorCallback = {})
{
    candidate.item.status = status;
    candidate.item.errorMessage = errorMessage;
    for (DuplicateItem &item : result.items) {
        if (item.path == candidate.item.path) {
            item = candidate.item;
            break;
        }
    }
    if (status == DuplicateHashStatus::Failed
        || status == DuplicateHashStatus::Changed) {
        reportError(
            result,
            candidate.item.path,
            errorMessage,
            errorCallback);
    }
    ++result.summary.processedFiles;
}

} // namespace

DuplicateResult DuplicateFinder::findDuplicates(
    const ScanResult &scanResult,
    const ScanCancellationToken &cancellationToken,
    const DuplicateProgressCallback &progressCallback,
    const DuplicateErrorCallback &errorCallback) const
{
    DuplicateResult result;

    try {
        std::map<qint64, std::vector<Candidate>> sizeGroups;
        for (const FileInfo &file : scanResult.files) {
            if (file.kind != FileKind::RegularFile || !file.isValid()) {
                continue;
            }
            ++result.summary.totalFiles;
            Candidate candidate;
            candidate.file = file;
            candidate.item.path = file.absolutePath;
            candidate.item.size = file.sizeBytes;
            candidate.item.modifiedUtc = file.modifiedUtc;
            sizeGroups[file.sizeBytes].push_back(std::move(candidate));
        }

        std::vector<Candidate *> candidates;
        for (auto &sizeGroup : sizeGroups) {
            auto &group = sizeGroup.second;
            std::sort(
                group.begin(),
                group.end(),
                [](const Candidate &left, const Candidate &right) {
                    return left.file.absolutePath < right.file.absolutePath;
                });
            if (group.size() < 2) {
                continue;
            }
            result.summary.candidateFiles += static_cast<qint64>(group.size());
            for (Candidate &candidate : group) {
                result.items.push_back(candidate.item);
                candidates.push_back(&candidate);
            }
        }

        reportProgress(
            result, DuplicatePhase::CandidateFiltering, QString(), progressCallback);

        for (Candidate *candidate : candidates) {
            if (cancellationToken.isCancelled()) {
                break;
            }
            reportProgress(
                result,
                DuplicatePhase::PartialHashing,
                candidate->file.absolutePath,
                progressCallback);
            const HashOutput partial =
                hashPartial(*candidate, cancellationToken);
            if (partial.cancelled) {
                break;
            }
            if (!partial.ok) {
                finalizeItem(
                    result,
                    *candidate,
                    partial.changed
                        ? DuplicateHashStatus::Changed
                        : DuplicateHashStatus::Failed,
                    partial.error,
                    errorCallback);
                reportProgress(
                    result,
                    DuplicatePhase::PartialHashing,
                    candidate->file.absolutePath,
                    progressCallback);
                continue;
            }

            candidate->item.partialHash = partial.digest;
            candidate->item.identity = partial.identity;
            candidate->item.status = DuplicateHashStatus::PartialHashed;
            ++result.summary.partialHashedFiles;
            for (DuplicateItem &item : result.items) {
                if (item.path == candidate->item.path) {
                    item = candidate->item;
                    break;
                }
            }
        }

        std::map<QByteArray, std::vector<Candidate *>> partialGroups;
        for (Candidate *candidate : candidates) {
            if (candidate->item.status == DuplicateHashStatus::PartialHashed) {
                partialGroups[candidate->item.partialHash].push_back(candidate);
            }
        }

        for (auto &partialGroup : partialGroups) {
            auto &group = partialGroup.second;
            if (group.size() < 2) {
                for (Candidate *candidate : group) {
                    finalizeItem(
                        result,
                        *candidate,
                        DuplicateHashStatus::NotDuplicate,
                        {},
                        errorCallback);
                    reportProgress(
                        result,
                        DuplicatePhase::PartialHashing,
                        candidate->file.absolutePath,
                        progressCallback);
                }
                continue;
            }

            std::map<QByteArray, std::vector<Candidate *>> fullGroups;
            for (Candidate *candidate : group) {
                if (cancellationToken.isCancelled()) {
                    break;
                }
                reportProgress(
                    result,
                    DuplicatePhase::FullHashing,
                    candidate->file.absolutePath,
                    progressCallback);
                const HashOutput full = hashFull(*candidate, cancellationToken);
                if (full.cancelled) {
                    break;
                }
                if (!full.ok) {
                    finalizeItem(
                        result,
                        *candidate,
                        full.changed
                            ? DuplicateHashStatus::Changed
                            : DuplicateHashStatus::Failed,
                        full.error,
                        errorCallback);
                    reportProgress(
                        result,
                        DuplicatePhase::FullHashing,
                        candidate->file.absolutePath,
                        progressCallback);
                    continue;
                }

                candidate->item.fullHash = full.digest;
                candidate->item.identity = full.identity;
                candidate->item.status = DuplicateHashStatus::FullHashed;
                ++result.summary.fullHashedFiles;
                for (DuplicateItem &item : result.items) {
                    if (item.path == candidate->item.path) {
                        item = candidate->item;
                        break;
                    }
                }
                fullGroups[full.digest].push_back(candidate);
            }

            for (auto &fullGroup : fullGroups) {
                auto &fullCandidates = fullGroup.second;
                if (fullCandidates.size() < 2) {
                    for (Candidate *candidate : fullCandidates) {
                        finalizeItem(
                            result,
                            *candidate,
                            DuplicateHashStatus::NotDuplicate,
                        {},
                        errorCallback);
                        reportProgress(
                            result,
                            DuplicatePhase::FullHashing,
                            candidate->file.absolutePath,
                            progressCallback);
                    }
                    continue;
                }

                DuplicateGroup duplicateGroup;
                duplicateGroup.groupId =
                    static_cast<qint64>(result.groups.size() + 1);
                duplicateGroup.fileSize = fullCandidates.front()->item.size;
                duplicateGroup.fullHash = fullGroup.first;
                duplicateGroup.duplicateCount =
                    static_cast<qint64>(fullCandidates.size());
                duplicateGroup.totalSize =
                    duplicateGroup.fileSize * duplicateGroup.duplicateCount;
                duplicateGroup.wastedSize =
                    duplicateGroup.fileSize * (duplicateGroup.duplicateCount - 1);
                for (Candidate *candidate : fullCandidates) {
                    duplicateGroup.items.push_back(candidate->item);
                    finalizeItem(
                        result,
                        *candidate,
                        DuplicateHashStatus::FullHashed,
                        {},
                        errorCallback);
                }
                result.groups.push_back(std::move(duplicateGroup));
                reportProgress(
                    result,
                    DuplicatePhase::FullHashing,
                    fullCandidates.front()->file.absolutePath,
                    progressCallback);
            }
        }

        for (Candidate *candidate : candidates) {
            if (candidate->item.status == DuplicateHashStatus::Pending
                || candidate->item.status == DuplicateHashStatus::PartialHashed) {
                candidate->item.status = cancellationToken.isCancelled()
                    ? DuplicateHashStatus::Cancelled
                    : DuplicateHashStatus::NotDuplicate;
                for (DuplicateItem &item : result.items) {
                    if (item.path == candidate->item.path) {
                        item = candidate->item;
                        break;
                    }
                }
                if (!cancellationToken.isCancelled()) {
                    ++result.summary.processedFiles;
                }
            }
        }

        for (const DuplicateGroup &group : result.groups) {
            result.summary.groupCount += 1;
            result.summary.duplicateFileCount += group.duplicateCount;
            result.summary.duplicateBytes += group.totalSize;
            result.summary.wastedBytes += group.wastedSize;
        }

        result.cancelled = cancellationToken.isCancelled();
        result.completed = true;
        result.state = result.cancelled
            ? TaskState::Cancelled
            : result.summary.errorCount > 0
                ? TaskState::CompletedWithErrors
                : TaskState::Completed;
        reportProgress(
            result,
            result.cancelled
                ? DuplicatePhase::Cancelled
                : result.summary.errorCount > 0
                    ? DuplicatePhase::Failed
                    : DuplicatePhase::Completed,
            QString(),
            progressCallback);
    } catch (const std::exception &exception) {
        result.completed = false;
        result.state = TaskState::Failed;
        result.fatalError =
            QStringLiteral("重复文件检测异常：%1")
                .arg(QString::fromLocal8Bit(exception.what()));
        reportProgress(
            result, DuplicatePhase::Failed, QString(), progressCallback);
    } catch (...) {
        result.completed = false;
        result.state = TaskState::Failed;
        result.fatalError = QStringLiteral("重复文件检测发生未知异常");
        reportProgress(
            result, DuplicatePhase::Failed, QString(), progressCallback);
    }

    return result;
}

} // namespace FilePilot
