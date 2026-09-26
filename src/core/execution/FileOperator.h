#pragma once

#include "core/execution/ExecutionTypes.h"
#include "core/filesystem/FileIdentity.h"

#include <QString>

#include <atomic>
#include <functional>
#include <optional>
#include <utility>

namespace FilePilot {

struct FileMoveRequest {
    FileMoveRequest() = default;

    FileMoveRequest(
        QString source,
        QString destination,
        const ConflictDecisionAction conflictAction,
        FileIdentity expectedDestination = {},
        FileIdentity expectedSource = {})
        : sourcePath(std::move(source))
        , destinationPath(std::move(destination))
        , action(conflictAction)
        , expectedDestinationIdentity(expectedDestination)
        , expectedSourceIdentity(expectedSource)
    {
    }

    QString sourcePath;
    QString destinationPath;
    ConflictDecisionAction action = ConflictDecisionAction::Proceed;
    FileIdentity expectedDestinationIdentity;
    FileIdentity expectedSourceIdentity;
};
using TemporaryFileVerifier = std::function<bool(
    const QString &sourcePath,
    const QString &temporaryPath,
    QString &error)>;

using BeforePublishHook = std::function<bool(
    const QString &sourcePath,
    const QString &destinationPath,
    QString &error)>;

struct FileMoveOptions {
    FileMoveOptions() = default;

    FileMoveOptions(
        TemporaryFileVerifier verifier,
        BeforePublishHook publishHook = {})
        : temporaryFileVerifier(std::move(verifier))
        , beforePublish(std::move(publishHook))
    {
    }

    TemporaryFileVerifier temporaryFileVerifier;
    BeforePublishHook beforePublish;
};
struct FileMoveResult {
    ExecutionItemStatus status = ExecutionItemStatus::Rejected;
    QString actualDestination;
    QString errorMessage;
    std::optional<PublishedMoveRecoveryState> publishedState;
    bool resumedPublishedResult = false;
};

class FileOperator
{
public:
    FileMoveResult resumePublishedCleanup(
        const FileMoveRequest &request,
        const PublishedMoveRecoveryState &recoveryState,
        const std::atomic_bool &cancelled) const;
    FileMoveResult move(
        const FileMoveRequest &request,
        const std::atomic_bool &cancelled,
        const FileMoveOptions &options = {}) const;
};

} // namespace FilePilot
