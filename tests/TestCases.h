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
    void excludesNonExecutableItems();
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
    void bindsPlanProvenance();
    void classifiesTargetRootKinds();
    void detectsNoOpPaths();
};

class OrganizePreviewModelTest : public QObject
{
    Q_OBJECT

private slots:
    void exposesRowsColumnsAndHeaders();
    void displaysPlanRows();
    void displaysNoOpStatus();
    void resetsAndClears();
};

class ExecutionResultModelTest : public QObject
{
    Q_OBJECT

private slots:
    void exposesRowsAndReadableStatuses();
    void formatsCompleteSummaryWithoutMergingCategories();
};
class ExecutionHistoryRepositoryTest : public QObject
{
    Q_OBJECT

private slots:
    void initializesSchemaAndSavesCompleteResult();
    void reopensAndDeletesHistoryWithItems();
    void persistsFinalStatesAndCleanupSemantics();
    void databaseFailureDoesNotModifyExecutionResult();
};

class HistoryPageTest : public QObject
{
    Q_OBJECT

private slots:
    void listsHistoryAndShowsSelectedDetails();
};
class DuplicateFinderTest : public QObject
{
    Q_OBJECT

private slots:
    void groupsExactDuplicatesAndCalculatesWastedSize();
    void rejectsDifferentContentAndPartialHashCollisions();
    void filtersDifferentSizesAndHandlesSmallAndLargeFiles();
    void reportsMissingChangedAndCancelledFiles();
    void computesSummaryAndErrorSemantics();
};

class DuplicateTaskTest : public QObject
{
    Q_OBJECT

private slots:
    void runsInBackgroundWithProgress();
    void cancellationStopsBeforeNewHashes();
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
    void generatesPreviewAndConfirmsWithoutFilesystemChanges();
    void mapsExecutionResultStatesToUi();
    void invalidatesPlanWhenTargetRootChanges();
    void invalidatesPlanWhenRescanning();
    void regeneratingPlanResetsConfirmation();
    void displaysNoOpPreview();
    void reportsInvalidDirectory();
};

class MainWindowTest : public QObject
{
    Q_OBJECT

private slots:
    void buildsRequiredShell();
    void switchesPagesThroughNavigation();
};

class ThemePaletteTest : public QObject
{
    Q_OBJECT

private slots:
    void derivesLightAndDarkPalettes();
    void derivesDefaultAndCustomAccents();
    void maintainsReadableAccentStatePairs();
    void usesHighContrastSystemSemantics();
    void fallsBackWhenSnapshotIsInvalid();
    void maintainsContrastRatios();
    void usesAccessibleTooltipQss();
    void usesValidFocusQss();
    void usesSelectionTextForSelectedStates();
    void exposesGlobalSemanticRoles();
    void usesModeColorsWhenClassicSystemColorsRemainLight();
};

class ThemeDetectorTest : public QObject
{
    Q_OBJECT

private slots:
    void readsCurrentSnapshot();
    void emitsOnlyChangedSnapshots();
    void usesModeFallbackPriority();
    void switchesLightDarkAccentAndHighContrast();
    void usesFallbackForInvalidState();
    void remainsOnGuiThread();
};

class MainWindowThemeSmokeTest : public QObject
{
    Q_OBJECT

private slots:
    void appliesThemeToMainWindow();
    void refreshesWithoutRecursivePaletteLoop();
    void stylesGlobalControlsAndRuntimeSwitches();
};

class DpiLayoutTest : public QObject
{
    Q_OBJECT

private slots:
    void keepsShellWithinViewport();
    void keepsDuplicatePageWithinViewport();
    void generatesLogicalSizedIcons();
};

} // namespace Test
} // namespace FilePilot
