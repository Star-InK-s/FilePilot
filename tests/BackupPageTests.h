#pragma once

#include <QObject>

namespace FilePilot {
namespace BackupUiTest {

class BackupPageContractTest : public QObject
{
    Q_OBJECT

private slots:
    void exposesRequiredControlsAndRunsPreview();
    void runsBackupAndShowsSummary();
};

} // namespace BackupUiTest
} // namespace FilePilot
