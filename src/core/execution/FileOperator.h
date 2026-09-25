#pragma once

#include "core/execution/ExecutionTypes.h"

#include <QString>

#include <atomic>

namespace FilePilot {

struct FileMoveRequest {
    QString sourcePath;
    QString destinationPath;
    ConflictDecisionAction action = ConflictDecisionAction::Proceed;
};

struct FileMoveResult {
    ExecutionItemStatus status = ExecutionItemStatus::Rejected;
    QString actualDestination;
    QString errorMessage;
};

class FileOperator
{
public:
    FileMoveResult move(
        const FileMoveRequest &request,
        const std::atomic_bool &cancelled) const;
};

} // namespace FilePilot
