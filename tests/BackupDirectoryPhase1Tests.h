#pragma once

#include <QObject>

namespace FilePilot {
namespace BackupDirectoryTest {

class BackupDirectoryPhase1ContractTest : public QObject
{
    Q_OBJECT

private slots:
    void nestedDirectoryTreeSucceedsAndPreservesSource();
    void emptyDirectorySucceedsAndPreservesSource();
    void rejectsUnsupportedReparseEntry();
    void enumerationFailureDoesNotPublishOrReportSuccess();
    void skipLeavesExistingDirectoryUnchanged();
    void autoRenameCreatesUniqueDirectory();
    void directoryOverwriteIsExplicitlyRejected();
    void cancelDuringCopyCleansStagingTree();
    void cancelBeforeVerifyCleansStagingTree();
    void cleanupFailureAfterPublishIsReported();
    void publishFailureCleansStagingTree();
};

} // namespace BackupDirectoryTest
} // namespace FilePilot
