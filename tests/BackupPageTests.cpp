#include "BackupPageTests.h"

#include "app/Application.h"
#include "ui/pages/BackupPage.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>

namespace FilePilot {
namespace BackupUiTest {
namespace {

bool writeTestFile(const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(contents) == contents.size();
}

} // namespace

void BackupPageContractTest::exposesRequiredControlsAndRunsPreview()
{
    auto &application = dynamic_cast<Application &>(*QCoreApplication::instance());
    BackupPage page(application);
    QVERIFY(page.findChild<QLineEdit *>(QStringLiteral("backupSourceEdit")) != nullptr);
    QVERIFY(page.findChild<QLineEdit *>(QStringLiteral("backupDestinationEdit")) != nullptr);
    QVERIFY(page.findChild<QComboBox *>(QStringLiteral("backupConflictPolicyCombo")) != nullptr);
    QVERIFY(page.findChild<QListWidget *>(QStringLiteral("backupPreviewList")) != nullptr);
    QVERIFY(page.findChild<QPushButton *>(QStringLiteral("startBackupButton")) != nullptr);
    QVERIFY(page.findChild<QPushButton *>(QStringLiteral("cancelBackupButton")) != nullptr);
    QVERIFY(page.findChild<QProgressBar *>(QStringLiteral("backupProgressBar")) != nullptr);
    QVERIFY(page.findChild<QLabel *>(QStringLiteral("backupCurrentFileLabel")) != nullptr);
    QVERIFY(page.findChild<QLabel *>(QStringLiteral("backupSummaryLabel")) != nullptr);
    QVERIFY(page.findChild<QLabel *>(QStringLiteral("backupErrorLabel")) != nullptr);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString destinationRoot =
        QDir(directory.path()).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("payload")));

    page.findChild<QLineEdit *>(QStringLiteral("backupSourceEdit"))->setText(source);
    page.findChild<QLineEdit *>(QStringLiteral("backupDestinationEdit"))->setText(destinationRoot);
    page.findChild<QPushButton *>(QStringLiteral("buildBackupPreviewButton"))->click();
    QVERIFY(page.findChild<QListWidget *>(QStringLiteral("backupPreviewList"))->count() > 0);
    QVERIFY(page.findChild<QPushButton *>(QStringLiteral("startBackupButton"))->isEnabled());
}

void BackupPageContractTest::runsBackupAndShowsSummary()
{
    auto &application = dynamic_cast<Application &>(*QCoreApplication::instance());
    BackupPage page(application);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QDir(directory.path()).filePath(QStringLiteral("source.txt"));
    const QString destinationRoot =
        QDir(directory.path()).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(destinationRoot));
    QVERIFY(writeTestFile(source, QByteArrayLiteral("payload")));

    page.findChild<QLineEdit *>(QStringLiteral("backupSourceEdit"))->setText(source);
    page.findChild<QLineEdit *>(QStringLiteral("backupDestinationEdit"))->setText(destinationRoot);
    page.findChild<QPushButton *>(QStringLiteral("buildBackupPreviewButton"))->click();
    page.findChild<QPushButton *>(QStringLiteral("startBackupButton"))->click();

    QLabel *summary = page.findChild<QLabel *>(QStringLiteral("backupSummaryLabel"));
    QTRY_VERIFY(summary->text().contains(QStringLiteral("结果：")));
    QVERIFY2(summary->text().contains(QStringLiteral("已发布：是")), qPrintable(summary->text()));
}

} // namespace BackupUiTest
} // namespace FilePilot

int main(int argc, char *argv[])
{
    QTemporaryDir dataDirectory;
    if (!dataDirectory.isValid()) {
        return 1;
    }
    FilePilot::Application application(argc, argv, dataDirectory.path());
    FilePilot::BackupUiTest::BackupPageContractTest test;
    return QTest::qExec(&test, argc, argv);
}
