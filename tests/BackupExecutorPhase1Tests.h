#pragma once

#include <QObject>

namespace FilePilot {
namespace BackupExecutorTest {

class BackupExecutorContractTest : public QObject
{
    Q_OBJECT

private slots:
    void singleRegularFileSucceedsAndPreservesSource();
    void failsWhenSourceMissing();
    void failsWhenDestinationDirectoryUnavailable();
    void failsOnCopyFailure();
    void failsOnVerificationMismatch();
    void failsWhenSourceChangedBeforePublish();
    void failsWhenSourceDeletedBeforePublish();
    void rejectsDestinationMutationBeforePublish();
    void cancelsBeforeCopy();
    void cancelsBeforePublish();
    void reportsTempCleanupFailure();
    void failsOnPublishFailure();
    void rejectsMismatchedPlannedSource();
    void rejectsDestinationOutsideDestinationRoot();
    void doesNotDeleteReplacementAtStagingPath();
    void skipLeavesExistingFileUnchanged();
    void autoRenameCreatesUniqueFile();
    void autoRenameRechecksNameBeforePublish();
    void overwriteReplacesExistingFile();
    void doesNotUseOrganizeMoveSemantics();
};

} // namespace BackupExecutorTest
} // namespace FilePilot
