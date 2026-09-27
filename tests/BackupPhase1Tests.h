#pragma once

#include <QObject>

namespace FilePilot {
namespace BackupTest {

class FileSnapshotContractTest : public QObject
{
    Q_OBJECT

private slots:
    void capturesRegularFileValues();
    void rejectsMissingFile();
    void detectsReplacementIdentityChange();
    void detectsSameSizeContentChange();
    void detectsSizeChange();
    void detectsDigestMismatch();
    void capturesDirectoryBasicInformation();
    void capturesReparseInformation();
};

class FileHasherContractTest : public QObject
{
    Q_OBJECT

private slots:
    void hashesFixedSmallFile();
    void hashesEmptyFile();
    void hashesMultiKilobyteFile();
    void hashesBinaryFile();
    void sameContentHasSameHash();
    void differentContentHasDifferentHash();
    void reportsReadFailure();
};

class BackupPlanContractTest : public QObject
{
    Q_OBJECT

private slots:
    void fileSourceCarriesSafetyMetadata();
    void directorySourceUsesRelativePath();
    void planItemExposesConflictAndDestinationIdentity();
    void planIsDataModelWithoutUiState();
};

class BackupInventoryBuilderContractTest : public QObject
{
    Q_OBJECT

private slots:
    void buildsNestedInventoryIncludingEmptyDirectories();
    void preservesDirectoryStructure();
    void reportsJunctionAsUnsupported();
    void reportsSymbolicLinkAsUnsupported();
    void reportsPermissionDeniedDirectory();
    void reportsChildEnumerationFailure();
    void emptyDirectoryRemainsComplete();
    void normalDirectoryRemainsComplete();
    void enumerationFailureIsNotComplete();
};

class BackupPrevalidatorContractTest : public QObject
{
    Q_OBJECT

private slots:
    void rejectsMissingSourceRoot();
    void acceptsMissingDestinationRootWhenCreatable();
    void rejectsEqualAndCaseEquivalentRoots();
    void rejectsNestedRootsInBothDirections();
    void rejectsReparsePointRoots();
    void exposesVolumeInformation();
    void rejectsInvalidAndOverlongPaths();
    void rejectsIntermediateJunctionEqualTarget();
    void rejectsIntermediateJunctionNestedTarget();
    void rejectsIntermediateJunctionInSourceChain();
    void rejectsIntermediateJunctionInDestinationChain();
    void rejectsJunctionWithMissingReplacementTarget();
};

} // namespace BackupTest
} // namespace FilePilot
