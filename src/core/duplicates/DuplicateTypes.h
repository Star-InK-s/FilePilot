#pragma once

#include "core/filesystem/FileIdentity.h"
#include "core/model/FileInfo.h"
#include "core/model/TaskState.h"

#include <QByteArray>
#include <QDateTime>
#include <QMetaType>
#include <QString>

#include <vector>

namespace FilePilot {

enum class DuplicateHashStatus {
    Pending,
    PartialHashed,
    FullHashed,
    NotDuplicate,
    Failed,
    Changed,
    Cancelled,
};

QString duplicateHashStatusName(DuplicateHashStatus status);

enum class DuplicatePhase {
    CandidateFiltering,
    PartialHashing,
    FullHashing,
    Completed,
    Cancelled,
    Failed,
};

QString duplicatePhaseName(DuplicatePhase phase);

struct DuplicateItem {
    QString path;
    qint64 size = 0;
    QDateTime modifiedUtc;
    DuplicateHashStatus status = DuplicateHashStatus::Pending;
    FileIdentity identity;
    QByteArray partialHash;
    QByteArray fullHash;
    QString errorMessage;
};

struct DuplicateGroup {
    qint64 groupId = 0;
    qint64 fileSize = 0;
    QByteArray fullHash;
    qint64 duplicateCount = 0;
    qint64 totalSize = 0;
    qint64 wastedSize = 0;
    std::vector<DuplicateItem> items;
};

struct DuplicateError {
    QString path;
    QString message;
    int code = 0;
};

struct DuplicateSummary {
    qint64 totalFiles = 0;
    qint64 candidateFiles = 0;
    qint64 processedFiles = 0;
    qint64 partialHashedFiles = 0;
    qint64 fullHashedFiles = 0;
    qint64 groupCount = 0;
    qint64 duplicateFileCount = 0;
    qint64 duplicateBytes = 0;
    qint64 wastedBytes = 0;
    qint64 errorCount = 0;
};

struct DuplicateProgress {
    qint64 processed = 0;
    qint64 total = 0;
    QString currentPath;
    DuplicatePhase phase = DuplicatePhase::CandidateFiltering;
    qint64 errorCount = 0;
};

struct DuplicateResult {
    TaskState state = TaskState::Idle;
    bool completed = false;
    bool cancelled = false;
    std::vector<DuplicateGroup> groups;
    std::vector<DuplicateItem> items;
    std::vector<DuplicateError> errors;
    DuplicateSummary summary;
    QString fatalError;
};

} // namespace FilePilot

Q_DECLARE_METATYPE(FilePilot::DuplicateItem)
Q_DECLARE_METATYPE(FilePilot::DuplicateGroup)
Q_DECLARE_METATYPE(FilePilot::DuplicateError)
Q_DECLARE_METATYPE(FilePilot::DuplicateProgress)
Q_DECLARE_METATYPE(FilePilot::DuplicateResult)
