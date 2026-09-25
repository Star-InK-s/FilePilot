#pragma once

#include <QObject>

namespace FilePilot {
namespace Test {

class CoreModelTest : public QObject
{
    Q_OBJECT

private slots:
    void fileInfoValidity();
    void taskStateNames();
    void taskStateTerminality();
    void appError();
};

class SettingsServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void storesValuesInIniFile();
};

class LogManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void writesEnabledEntries();
};

class OrganizePlanTest : public QObject
{
    Q_OBJECT

private slots:
    void storesAndClearsItems();
    void countsStatusesAndCategories();
};

class OrganizePlannerTest : public QObject
{
    Q_OBJECT

private slots:
    void plansDocumentsAndImages();
    void plansMultipleCategories();
    void handlesEmptyScanResult();
    void rejectsUnsafeCategories();
    void rejectsUnsafeFileNames();
    void supportsUnicodeAndLongPaths();
    void marksInvalidItemsWithoutFilesystemChecks();
};

class OrganizePreviewModelTest : public QObject
{
    Q_OBJECT

private slots:
    void exposesRowsColumnsAndHeaders();
    void displaysPlanRows();
    void resetsAndClears();
};

class RuleEngineTest : public QObject
{
    Q_OBJECT

private slots:
    void usesDefaultCategories();
    void customRulesBeatOthersFallback();
    void othersIsOnlyFinalFallback();
    void rejectsEmptyCategoryRules();
    void usesCustomRules();
    void appliesPriorityOrder();
    void ignoresDisabledRules();
    void normalizesExtensions();
    void writesCategoriesToScanResult();
};

class ScanServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void emptyDirectory();
    void scansFilesAndStatistics();
    void scansNestedUnicodeAndSpecialPaths();
    void rejectsMissingDirectory();
    void recordsFileAccessFailureWithoutStopping();
    void entryErrorDoesNotStopSiblingScan();
    void respectsCancellation();
    void reportsExactErrorCountWithBoundedDetails();
    void windowsOrdinaryDirectoryScan();
    void windowsRootJunctionIsRejected();
    void windowsNestedJunctionIsSkipped();
    void windowsJunctionCycleIsSafe();
};

class ScanTaskTest : public QObject
{
    Q_OBJECT

private slots:
    void runsWithoutBlocking();
    void canBeCancelled();
    void preparingCancellationIsStable();
    void runningCancellationIsStable();
    void completionAndCancellationRaceIsStable();
    void repeatedCancelAndStartAreStable();
    void flushesErrorBatchesWithoutDroppingCounts();
};

class FileTableModelTest : public QObject
{
    Q_OBJECT

private slots:
    void exposesRowsColumnsAndHeaders();
    void formatsFileRows();
};

class FileOrganizePageTest : public QObject
{
    Q_OBJECT

private slots:
    void scansAndDisplaysResults();
    void categorySummaryShowsAllCategories();
    void reportsInvalidDirectory();
};

class MainWindowTest : public QObject
{
    Q_OBJECT

private slots:
    void buildsRequiredShell();
    void switchesPagesThroughNavigation();
};

} // namespace Test
} // namespace FilePilot
