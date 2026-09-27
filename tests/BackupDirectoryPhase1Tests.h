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
};

} // namespace BackupDirectoryTest
} // namespace FilePilot
