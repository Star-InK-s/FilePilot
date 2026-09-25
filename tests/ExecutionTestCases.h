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
};

class OrganizeExecutionTaskTest : public QObject
{
    Q_OBJECT

private slots:
    void executesPlan();
    void reportsPartialFailure();
    void supportsCancellation();
    void rejectsRepeatedStartAndDoesNotBlockUi();
};

} // namespace Test
} // namespace FilePilot
