#pragma once

#include <QObject>

namespace FilePilot {
namespace BackupHistoryTest {

class BackupHistoryContractTest : public QObject
{
    Q_OBJECT

private slots:
    void persistsBackupResultAndSupportsLookup();
    void historyFailureDoesNotMutateFilesystemResult();
    void preservesDistinctTerminalStatuses();
};

} // namespace BackupHistoryTest
} // namespace FilePilot
