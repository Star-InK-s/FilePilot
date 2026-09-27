#pragma once

#include <QObject>

namespace FilePilot {
namespace BackupTaskTest {

class BackupTaskContractTest : public QObject
{
    Q_OBJECT

private slots:
    void runsSingleFileAndReportsLifecycle();
    void rejectsRepeatedStartWhileActive();
    void terminalStateIsStableAndReusable();
    void reportsFailedFinalResultForInvalidPlan();
};

} // namespace BackupTaskTest
} // namespace FilePilot
