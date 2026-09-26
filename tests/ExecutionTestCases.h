#pragma once

#include <QObject>

namespace FilePilot {
namespace Test {

class OrganizeExecutionPrevalidatorTest : public QObject
{
    Q_OBJECT

private slots:
    void validatesFreshCandidate();
    void rejectsChangedOrMissingSource();
    void rejectsReparseAndInvalidProvenance();
    void rejectsUnsafeDestinationAndNoOp();
    void detectsTargetRootIdentityChange();
    void rejectsDestinationReparseReplacementAfterValidation();
};

class ConflictResolverTest : public QObject
{
    Q_OBJECT

private slots:
    void resolvesNoConflictAndPolicies();
    void generatesSafeAutoRename();
    void rejectsPlanInternalConflicts();
    void detectsTargetAppearingAfterCheck();
};

class FileOperatorTest : public QObject
{
    Q_OBJECT

private slots:
    void movesWithinVolume();
    void movesAcrossVolumes();
    void preservesSourceOnFailure();
    void preservesDestinationOnFailedOverwrite();
    void handlesUnicodePaths();
    void reportsSourceCleanupFailure();
    void handlesSourceInUse();
    void failsClosedOnCrossVolumeTemporaryVerificationFailure();
    void rejectsOverwriteTargetIdentityChangeAfterConflictCheck();
    void rejectsSourceContentChangeAfterTemporaryVerification();
    void rejectsStableIdentitySourceContentChangeAfterVerification();
    void rejectsDestinationParentJunctionReplacementAfterVerification();
    void rejectsSourceParentJunctionReplacementAfterVerification();
    void requiresTestSeamBetweenFinalIdentityCheckAndReplace();
    void cancelsBeforePublishWithoutFilesystemSideEffects();
    void requiresAtomicFileSnapshotSeam();
    void requiresEnsureParentDirectoryCheckToMkdirSeam();
    void resumePublishedCleanupRejectsMismatchWithoutResumedFlag();
    void resumePublishedCleanupSetsResumedFlagOnSuccess();
};

class OrganizeExecutionTaskTest : public QObject
{
    Q_OBJECT

private slots:
    void executesPlan();
    void reportsPartialFailure();
    void supportsCancellation();
    void rejectsRepeatedStartAndDoesNotBlockUi();
    void sourceCleanupFailureDoesNotCreateSecondCopyOnRetry();
    void rejectsForeignSourceOutsideScanSourceRoot();
    void doesNotReuseRecoveryStateAcrossExecutionContexts();
};

} // namespace Test
} // namespace FilePilot
