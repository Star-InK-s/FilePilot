#include "ExecutionTestCases.h"
#include "TestCases.h"

#include "app/Application.h"
#include "app/MainWindow.h"
#include "platform/windows/WindowsThemeDetector.h"
#include "ui/presenters/DuplicateTheme.h"
#include "ui/theme/QtThemeApplier.h"
#include "ui/theme/ThemePalette.h"
#include "ui/theme/ThemeSnapshot.h"
#include "core/classify/RuleEngine.h"
#include "core/database/ExecutionHistoryRepository.h"
#include "core/duplicates/DuplicateFinder.h"
#include "core/duplicates/DuplicateTask.h"
#include "core/organize/OrganizePlan.h"
#include "core/organize/OrganizePathValidator.h"
#include "core/organize/OrganizePlanner.h"
#include "core/logging/LogManager.h"
#include "core/model/AppError.h"
#include "core/model/FileInfo.h"
#include "core/model/TaskState.h"
#include "core/scan/ScanService.h"
#include "core/tasks/ScanTask.h"
#include "ui/models/ExecutionResultModel.h"
#include "ui/models/FileTableModel.h"
#include "ui/models/OrganizePreviewModel.h"
#include "ui/pages/DuplicateFilesPage.h"
#include "ui/pages/FileOrganizePage.h"
#include "ui/pages/Pages.h"
#include "core/settings/SettingsService.h"

#include <QCoreApplication>
#include <QtMath>


#include <algorithm>
#include <QDir>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QListWidget>
#include <QProcess>
#include <QProgressBar>
#include <QRegularExpression>
#include <QPushButton>
#include <QSignalSpy>
#include <QLabel>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QStackedWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTextStream>
#include <QToolBar>
#include <QThread>
#include <QWidget>
#include <QUuid>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace FilePilot {
namespace Test {

namespace {

Application &testApplication()
{
    return *static_cast<Application *>(QCoreApplication::instance());
}

qreal srgbLuminance(const QColor &color)
{
    const auto linear = [](const qreal value) {
        return value <= 0.04045
            ? value / 12.92
            : qPow((value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(color.redF())
        + 0.7152 * linear(color.greenF())
        + 0.0722 * linear(color.blueF());
}

qreal contrastRatio(const QColor &first, const QColor &second)
{
    const qreal firstLuminance = srgbLuminance(first);
    const qreal secondLuminance = srgbLuminance(second);
    const qreal lighter = std::max(firstLuminance, secondLuminance);
    const qreal darker = std::min(firstLuminance, secondLuminance);
    return (lighter + 0.05) / (darker + 0.05);
}

int colorDistance(const QColor &first, const QColor &second)
{
    return qAbs(first.red() - second.red())
        + qAbs(first.green() - second.green())
        + qAbs(first.blue() - second.blue());
}

QString qssRuleBody(const QString &styleSheet, const QString &selector)
{
    const qsizetype selectorIndex = styleSheet.indexOf(selector);
    if (selectorIndex < 0) {
        return {};
    }

    const qsizetype openIndex = styleSheet.indexOf(QLatin1Char('{'), selectorIndex);
    const qsizetype closeIndex = styleSheet.indexOf(QLatin1Char('}'), openIndex + 1);
    if (openIndex < 0 || closeIndex < 0) {
        return {};
    }

    return styleSheet.mid(openIndex + 1, closeIndex - openIndex - 1);
}

QString qssDeclarationValue(const QString &body, const QString &property)
{
    const QStringList declarations = body.split(QLatin1Char(';'));
    for (const QString &declaration : declarations) {
        const QString trimmed = declaration.trimmed();
        const qsizetype separator = trimmed.indexOf(QLatin1Char(':'));
        if (separator < 0) {
            continue;
        }
        if (trimmed.left(separator).trimmed() == property) {
            return trimmed.mid(separator + 1).trimmed();
        }
    }
    return {};
}

ThemeSnapshot highContrastSelectionSnapshot()
{
    ThemeSnapshot snapshot;
    snapshot.mode = ThemeMode::Light;
    snapshot.highContrast = true;
    snapshot.valid = true;
    snapshot.background = QColor(QStringLiteral("#000000"));
    snapshot.foreground = QColor(QStringLiteral("#FFFFFF"));
    snapshot.highlight = QColor(QStringLiteral("#0000FF"));
    snapshot.highlightText = QColor(QStringLiteral("#FFFF00"));
    snapshot.buttonFace = QColor(QStringLiteral("#202020"));
    snapshot.buttonText = QColor(QStringLiteral("#FFFFFF"));
    snapshot.hotLight = QColor(QStringLiteral("#00FF00"));
    snapshot.windowFrame = QColor(QStringLiteral("#FFFFFF"));
    snapshot.grayText = QColor(QStringLiteral("#A0A0A0"));
    return snapshot;
}

ThemeSnapshot themeSnapshot(const ThemeMode mode,
                            const QColor &accent,
                            const bool highContrast = false)
{
    ThemeSnapshot snapshot;
    snapshot.mode = mode;
    snapshot.highContrast = highContrast;
    snapshot.accent = accent;
    snapshot.valid = true;

    if (mode == ThemeMode::Dark) {
        snapshot.background = QColor(QStringLiteral("#202020"));
        snapshot.foreground = QColor(QStringLiteral("#F5F5F5"));
    } else {
        snapshot.background = QColor(QStringLiteral("#F3F3F3"));
        snapshot.foreground = QColor(QStringLiteral("#1A1A1A"));
    }

    snapshot.highlight = QColor(QStringLiteral("#0078D4"));
    snapshot.highlightText = QColor(QStringLiteral("#FFFFFF"));
    snapshot.buttonFace = QColor(QStringLiteral("#F0F0F0"));
    snapshot.buttonText = QColor(QStringLiteral("#1A1A1A"));
    snapshot.hotLight = QColor(QStringLiteral("#0067C0"));
    snapshot.windowFrame = QColor(QStringLiteral("#707070"));
    snapshot.grayText = QColor(QStringLiteral("#767676"));
    return snapshot;
}

bool visibleChildrenFit(const QWidget &widget)
{
    const QRect bounds = widget.rect();
    const auto children = widget.findChildren<QWidget *>();
    for (const QWidget *child : children) {
        if (!child->isVisible()) {
            continue;
        }

        const QPoint topLeft = child->mapTo(&widget, QPoint(0, 0));
        const QRect geometry(topLeft, child->size());
        if (!bounds.contains(geometry)) {
            return false;
        }
    }
    return true;
}

} // namespace

void CoreModelTest::fileInfoValidity()
{
    FileInfo empty;
    QVERIFY(!empty.isValid());

    FileInfo file;
    file.absolutePath = QStringLiteral("C:/Data/report.pdf");
    file.fileName = QStringLiteral("report.pdf");
    file.extension = QStringLiteral("pdf");
    file.sizeBytes = 128;
    file.kind = FileKind::RegularFile;
    file.category = QStringLiteral("Documents");

    QVERIFY(file.isValid());
    QCOMPARE(file.sizeBytes, 128);

    FileInfo copy = file;
    QCOMPARE(copy, file);
}

void CoreModelTest::taskStateNames()
{
    QCOMPARE(taskStateName(TaskState::Idle), QStringLiteral("Idle"));
    QCOMPARE(taskStateName(TaskState::Running), QStringLiteral("Running"));
    QCOMPARE(taskStateName(TaskState::CompletedWithErrors),
             QStringLiteral("Completed with errors"));
    QCOMPARE(taskStateName(TaskState::Failed), QStringLiteral("Failed"));
}

void CoreModelTest::taskStateTerminality()
{
    QVERIFY(!isTerminalTaskState(TaskState::Idle));
    QVERIFY(!isTerminalTaskState(TaskState::Preparing));
    QVERIFY(!isTerminalTaskState(TaskState::Running));
    QVERIFY(!isTerminalTaskState(TaskState::Cancelling));
    QVERIFY(isTerminalTaskState(TaskState::Completed));
    QVERIFY(isTerminalTaskState(TaskState::CompletedWithErrors));
    QVERIFY(isTerminalTaskState(TaskState::Cancelled));
    QVERIFY(isTerminalTaskState(TaskState::Failed));
}

void CoreModelTest::appError()
{
    const AppError empty;
    QVERIFY(!empty.isValid());

    const AppError error(ErrorCode::AccessDenied,
                         QStringLiteral("Cannot read file"),
                         QStringLiteral("C:/Data/private.pdf"));
    QVERIFY(error.isValid());
    QCOMPARE(error.code(), ErrorCode::AccessDenied);
    QCOMPARE(error.message(), QStringLiteral("Cannot read file"));
    QCOMPARE(error.context(), QStringLiteral("C:/Data/private.pdf"));
    QCOMPARE(errorCodeName(error.code()), QStringLiteral("Access denied"));
}

namespace {

bool writeFile(const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    return file.write(contents) == contents.size();
}

#ifdef Q_OS_WIN
QString windowsLongPath(const QString &path)
{
    const QString nativePath = QDir::toNativeSeparators(path);
    return nativePath.startsWith(QStringLiteral("\\\\?\\"))
        ? nativePath
        : QStringLiteral("\\\\?\\") + nativePath;
}

bool createTrailingDotFile(const QString &path, const QByteArray &contents)
{
    const QString nativePath = windowsLongPath(path);
    HANDLE handle = CreateFileW(
        reinterpret_cast<const wchar_t *>(nativePath.utf16()),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD written = 0;
    const BOOL success = WriteFile(
        handle,
        contents.constData(),
        static_cast<DWORD>(contents.size()),
        &written,
        nullptr);
    CloseHandle(handle);

    return success != FALSE && written == static_cast<DWORD>(contents.size());
}

#ifdef Q_OS_WIN
bool createJunction(const QString &junctionPath, const QString &targetPath)
{
    QDir().mkpath(QFileInfo(junctionPath).absolutePath());
    const QString command =
        QStringLiteral("New-Item -ItemType Junction -Path '%1' -Target '%2' | Out-Null")
            .arg(junctionPath, targetPath);
    return QProcess::execute(
        QStringLiteral("powershell.exe"),
        {
            QStringLiteral("-NoProfile"),
            QStringLiteral("-Command"),
            command,
        }) == 0;
}
#endif
void removeTrailingDotFile(const QString &path)
{
    const QString nativePath = windowsLongPath(path);
    DeleteFileW(reinterpret_cast<const wchar_t *>(nativePath.utf16()));
}
#endif

} // namespace

namespace {

FileInfo fileWithExtension(const QString &extension)
{
    FileInfo file;
    file.absolutePath = QStringLiteral("C:/Data/file");
    file.fileName = QStringLiteral("file");
    file.extension = extension;
    return file;
}

} // namespace

namespace {

FileInfo planFile(const QString &category,
                  const QString &fileName,
                  const QString &sourcePath = QStringLiteral("C:/source/file"))
{
    FileInfo file;
    file.absolutePath = sourcePath;
    file.fileName = fileName;
    file.extension = fileName.contains(QLatin1Char('.'))
        ? fileName.section(QLatin1Char('.'), -1).toLower()
        : QString();
    file.sizeBytes = 42;
    file.modifiedUtc = QDateTime::fromMSecsSinceEpoch(1000, QTimeZone::UTC);
    file.category = category;
    return file;
}

OrganizePlanItem plannedItem(const QString &category,
                             const QString &fileName)
{
    return OrganizePlanItem{
        QStringLiteral("C:/source/") + fileName,
        QStringLiteral("D:/target/") + category + QLatin1Char('/') + fileName,
        category,
        fileName,
        42,
        QDateTime::fromMSecsSinceEpoch(1000, QTimeZone::UTC),
        OrganizePlanStatus::Planned,
        QString(),
    };
}

} // namespace

void OrganizePlanTest::storesAndClearsItems()
{
    OrganizePlan plan;
    QVERIFY(plan.isEmpty());
    QCOMPARE(plan.count(), std::size_t{0});

    plan.add(plannedItem(QStringLiteral("Documents"), QStringLiteral("a.pdf")));
    plan.add(plannedItem(QStringLiteral("Images"), QStringLiteral("b.png")));
    QCOMPARE(plan.count(), std::size_t{2});
    QVERIFY(!plan.isEmpty());

    plan.clear();
    QVERIFY(plan.isEmpty());
    QCOMPARE(plan.count(), std::size_t{0});
}

void OrganizePlanTest::countsStatusesAndCategories()
{
    OrganizePlan plan;
    plan.add(plannedItem(QStringLiteral("Documents"), QStringLiteral("a.pdf")));

    OrganizePlanItem invalid = plannedItem(QStringLiteral("Images"), QStringLiteral("b.png"));
    invalid.planStatus = OrganizePlanStatus::Invalid;
    plan.add(invalid);

    QCOMPARE(plan.plannedCount(), 1);
    QCOMPARE(plan.invalidCount(), 1);

    const auto counts = plan.categoryCounts();
    QCOMPARE(counts.value(QStringLiteral("Documents")), 1);
    QCOMPARE(counts.value(QStringLiteral("Images")), 1);
}

void OrganizePlannerTest::plansDocumentsAndImages()
{
    ScanResult scan;
    scan.files.push_back(planFile(QStringLiteral("Documents"), QStringLiteral("test.pdf")));
    scan.files.push_back(planFile(QStringLiteral("Images"), QStringLiteral("photo.jpg")));

    const OrganizePlanner planner(QStringLiteral("D:/Downloads/Organized"));
    const OrganizePlan plan = planner.plan(scan);

    QCOMPARE(plan.count(), std::size_t{2});
    QCOMPARE(plan.items().at(0).destinationPath,
             QStringLiteral("D:/Downloads/Organized/Documents/test.pdf"));
    QCOMPARE(plan.items().at(1).destinationPath,
             QStringLiteral("D:/Downloads/Organized/Images/photo.jpg"));
    QCOMPARE(plan.plannedCount(), 2);
    QCOMPARE(plan.invalidCount(), 0);
}

void OrganizePlannerTest::plansMultipleCategories()
{
    ScanResult scan;
    scan.files.push_back(planFile(QStringLiteral("Documents"), QStringLiteral("a.pdf")));
    scan.files.push_back(planFile(QStringLiteral("Audio"), QStringLiteral("b.mp3")));
    scan.files.push_back(planFile(QStringLiteral("Programming"), QStringLiteral("c.cpp")));

    const OrganizePlan plan =
        OrganizePlanner(QStringLiteral("D:/target")).plan(scan);

    QCOMPARE(plan.count(), std::size_t{3});
    QCOMPARE(plan.categoryCounts().size(), 3);
    QCOMPARE(plan.plannedCount(), 3);
}

void OrganizePlannerTest::handlesEmptyScanResult()
{
    const OrganizePlan plan =
        OrganizePlanner(QStringLiteral("D:/target")).plan(ScanResult{});
    QVERIFY(plan.isEmpty());
    QCOMPARE(plan.plannedCount(), 0);
    QCOMPARE(plan.invalidCount(), 0);
}

void OrganizePlanTest::excludesNonExecutableItems()
{
    OrganizePlan plan;
    OrganizePlanItem planned = plannedItem(QStringLiteral("Documents"), QStringLiteral("a.pdf"));
    OrganizePlanItem invalid = plannedItem(QStringLiteral("Images"), QStringLiteral("b.png"));
    invalid.planStatus = OrganizePlanStatus::Invalid;
    OrganizePlanItem noOp = plannedItem(QStringLiteral("Audio"), QStringLiteral("c.mp3"));
    noOp.planStatus = OrganizePlanStatus::NoOp;
    plan.add(std::move(planned));
    plan.add(std::move(invalid));
    plan.add(std::move(noOp));

    QCOMPARE(plan.count(), std::size_t{3});
    QCOMPARE(plan.plannedItems().size(), std::size_t{1});
    QCOMPARE(plan.executableCandidates().size(), std::size_t{1});
    QCOMPARE(plan.executableCandidates().at(0).fileName, QStringLiteral("a.pdf"));
    QCOMPARE(plan.plannedCount(), 1);
    QCOMPARE(plan.invalidCount(), 1);
    QCOMPARE(plan.noOpCount(), 1);
}

void OrganizePlannerTest::bindsPlanProvenance()
{
    ScanResult scan;
    scan.rootPath = QStringLiteral("D:/Source");
    scan.files.push_back(planFile(QStringLiteral("Documents"), QStringLiteral("a.pdf")));

    const OrganizePlan plan = OrganizePlanner(QStringLiteral("D:/Target/../Target"))
                                  .plan(scan, 7, 3);

    QCOMPARE(plan.provenance().normalizedTargetRoot, QStringLiteral("D:/Target"));
    QCOMPARE(plan.provenance().targetRootKind, TargetRootKind::Absolute);
    QCOMPARE(plan.provenance().planGeneration, quint64{7});
    QCOMPARE(plan.provenance().scanGeneration, quint64{3});
    QCOMPARE(plan.provenance().scanSourceRoot, QStringLiteral("D:/Source"));
    QVERIFY(plan.isCurrentFor(
        QStringLiteral("d:\\target\\"), 7, 3, QStringLiteral("d:/source")));
    QVERIFY(!plan.isCurrentFor(
        QStringLiteral("D:/Target"), 8, 3, QStringLiteral("D:/Source")));
    QVERIFY(!plan.isCurrentFor(
        QStringLiteral("D:/Target"), 7, 4, QStringLiteral("D:/Source")));
    QVERIFY(!plan.isCurrentFor(
        QStringLiteral("D:/Other"), 7, 3, QStringLiteral("D:/Source")));
}

void OrganizePlannerTest::classifiesTargetRootKinds()
{
    QCOMPARE(OrganizePathValidator::inspectTargetRoot(QString()).kind,
             TargetRootKind::Empty);
    QCOMPARE(OrganizePathValidator::inspectTargetRoot(QStringLiteral("relative/path")).kind,
             TargetRootKind::Relative);
    QCOMPARE(OrganizePathValidator::inspectTargetRoot(QStringLiteral("D:/Target")).kind,
             TargetRootKind::Absolute);
    QCOMPARE(OrganizePathValidator::inspectTargetRoot(QStringLiteral("//server/share")).kind,
             TargetRootKind::Unc);
    QCOMPARE(OrganizePathValidator::inspectTargetRoot(QStringLiteral("//?/C:/Target")).kind,
             TargetRootKind::DeviceNamespace);
}

void OrganizePlannerTest::detectsNoOpPaths()
{
    ScanResult scan;
    FileInfo exact = planFile(
        QStringLiteral("Documents"),
        QStringLiteral("a.txt"),
        QStringLiteral("D:/target/Documents/a.txt"));
    scan.files.push_back(exact);

    FileInfo caseEquivalent = planFile(
        QStringLiteral("Documents"),
        QStringLiteral("A.TXT"),
        QStringLiteral("d:/TARGET/documents/A.TXT"));
    scan.files.push_back(caseEquivalent);

    FileInfo normalizedEquivalent = planFile(
        QStringLiteral("Documents"),
        QStringLiteral("A.txt"),
        QStringLiteral("D:/target/Documents/../Documents/A.txt"));
    scan.files.push_back(normalizedEquivalent);

    const OrganizePlan plan =
        OrganizePlanner(QStringLiteral("D:/target")).plan(scan);
    QCOMPARE(plan.count(), std::size_t{3});
    QCOMPARE(plan.noOpCount(), 3);
    QCOMPARE(plan.plannedCount(), 0);
    QVERIFY(plan.executableCandidates().empty());
    for (const OrganizePlanItem &item : plan.items()) {
        QCOMPARE(item.planStatus, OrganizePlanStatus::NoOp);
    }
}
void OrganizePlannerTest::rejectsUnsafeCategories()
{
    const QStringList invalidCategories{
        QString(),
        QStringLiteral("   "),
        QStringLiteral("."),
        QStringLiteral(".."),
        QStringLiteral("../Other"),
        QStringLiteral("C:/Other"),
        QStringLiteral("Bad/Path"),
        QStringLiteral("CON"),
        QStringLiteral("CON.txt"),
        QStringLiteral("bad "),
        QStringLiteral("bad."),
        QStringLiteral("a:b"),
        QStringLiteral("a*b"),
    };

    ScanResult scan;
    for (const QString &category : invalidCategories) {
        scan.files.push_back(planFile(category, QStringLiteral("file.txt")));
    }

    const OrganizePlan plan =
        OrganizePlanner(QStringLiteral("D:/target")).plan(scan);
    QCOMPARE(plan.count(), invalidCategories.size());
    QCOMPARE(plan.invalidCount(), invalidCategories.size());
    for (const OrganizePlanItem &item : plan.items()) {
        QCOMPARE(item.planStatus, OrganizePlanStatus::Invalid);
        QVERIFY(!item.errorMessage.isEmpty());
        QVERIFY(item.destinationPath.isEmpty());
    }
}

void OrganizePlannerTest::rejectsUnsafeFileNames()
{
    const QStringList invalidFileNames{
        QString(),
        QStringLiteral("."),
        QStringLiteral(".."),
        QStringLiteral("bad/name.txt"),
        QStringLiteral("bad\\name.txt"),
        QStringLiteral("CON.txt"),
        QStringLiteral("bad .txt "),
        QStringLiteral("bad.txt."),
    };

    ScanResult scan;
    for (const QString &fileName : invalidFileNames) {
        scan.files.push_back(planFile(QStringLiteral("Documents"), fileName));
    }

    const OrganizePlan plan =
        OrganizePlanner(QStringLiteral("D:/target")).plan(scan);
    QCOMPARE(plan.invalidCount(), invalidFileNames.size());
    for (const OrganizePlanItem &item : plan.items()) {
        QCOMPARE(item.planStatus, OrganizePlanStatus::Invalid);
    }
}

void OrganizePlannerTest::supportsUnicodeAndLongPaths()
{
    const QString longName = QString(220, QLatin1Char('a')) + QStringLiteral(".pdf");
    ScanResult scan;
    scan.files.push_back(planFile(
        QStringLiteral("Documents"),
        QStringLiteral("报告 最终.pdf"),
        QStringLiteral("D:/源目录/报告 最终.pdf")));
    scan.files.push_back(planFile(
        QStringLiteral("Images"),
        longName,
        QStringLiteral("D:/源目录/") + longName));

    const OrganizePlan plan =
        OrganizePlanner(QStringLiteral("D:/中文目标根目录/Organized")).plan(scan);

    QCOMPARE(plan.invalidCount(), 0);
    QCOMPARE(
        plan.items().at(0).destinationPath,
        QStringLiteral("D:/中文目标根目录/Organized/Documents/报告 最终.pdf"));
    QVERIFY(plan.items().at(1).destinationPath.endsWith(longName));
}

void OrganizePlannerTest::marksInvalidItemsWithoutFilesystemChecks()
{
    ScanResult scan;
    scan.files.push_back(planFile(QStringLiteral("Documents"), QStringLiteral("valid.pdf")));
    scan.files.push_back(planFile(QStringLiteral("../Escape"), QStringLiteral("bad.pdf")));

    const OrganizePlan plan =
        OrganizePlanner(QStringLiteral("D:/target")).plan(scan);
    QCOMPARE(plan.count(), std::size_t{2});
    QCOMPARE(plan.items().at(0).planStatus, OrganizePlanStatus::Planned);
    QCOMPARE(plan.items().at(1).planStatus, OrganizePlanStatus::Invalid);
    QVERIFY(plan.items().at(1).destinationPath.isEmpty());
}

void OrganizePreviewModelTest::exposesRowsColumnsAndHeaders()
{
    OrganizePreviewModel model;
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.columnCount(), 5);
    QCOMPARE(model.headerData(0, Qt::Horizontal).toString(), QStringLiteral("文件名"));
    QCOMPARE(model.headerData(1, Qt::Horizontal).toString(), QStringLiteral("当前路径"));
    QCOMPARE(model.headerData(2, Qt::Horizontal).toString(), QStringLiteral("分类"));
    QCOMPARE(model.headerData(3, Qt::Horizontal).toString(), QStringLiteral("目标路径"));
    QCOMPARE(model.headerData(4, Qt::Horizontal).toString(), QStringLiteral("状态"));
}

void OrganizePreviewModelTest::displaysPlanRows()
{
    OrganizePlan plan;
    plan.add(plannedItem(QStringLiteral("Documents"), QStringLiteral("test.pdf")));
    OrganizePlanItem invalid = plannedItem(QStringLiteral("Images"), QStringLiteral("bad.png"));
    invalid.planStatus = OrganizePlanStatus::Invalid;
    invalid.errorMessage = QStringLiteral("invalid category");
    plan.add(invalid);

    OrganizePreviewModel model;
    model.setPlan(std::move(plan));

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0, 0)).toString(), QStringLiteral("test.pdf"));
    QCOMPARE(model.data(model.index(0, 2)).toString(), QStringLiteral("Documents"));
    QCOMPARE(model.data(model.index(0, 4)).toString(), QStringLiteral("Planned"));
    QCOMPARE(model.data(model.index(1, 4)).toString(), QStringLiteral("Invalid"));
    QCOMPARE(model.data(model.index(1, 3)).toString(), QStringLiteral("—"));
    QCOMPARE(model.data(model.index(1, 4), Qt::ToolTipRole).toString(),
             QStringLiteral("invalid category"));
}

void OrganizePreviewModelTest::displaysNoOpStatus()
{
    OrganizePlan plan;
    OrganizePlanItem item = plannedItem(QStringLiteral("Documents"), QStringLiteral("same.txt"));
    item.planStatus = OrganizePlanStatus::NoOp;
    plan.add(std::move(item));

    OrganizePreviewModel model;
    model.setPlan(std::move(plan));

    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 4)).toString(), QStringLiteral("NoOp"));
    QVERIFY(!model.data(model.index(0, 3)).toString().isEmpty());
}
void OrganizePreviewModelTest::resetsAndClears()
{
    OrganizePlan plan;
    plan.add(plannedItem(QStringLiteral("Documents"), QStringLiteral("test.pdf")));

    OrganizePreviewModel model;
    model.setPlan(std::move(plan));
    QCOMPARE(model.rowCount(), 1);

    model.clear();
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(model.plan().isEmpty());
}
void ExecutionResultModelTest::exposesRowsAndReadableStatuses()
{
    ExecutionResult result;
    OrganizePlanItem first;
    first.sourcePath = QStringLiteral("C:/source/first.txt");
    first.destinationPath = QStringLiteral("D:/target/first.txt");
    OrganizePlanItem second;
    second.sourcePath = QStringLiteral("C:/source/second.txt");
    second.destinationPath = QStringLiteral("D:/target/second.txt");

    result.items = {
        ExecutionItemResult{
            first,
            first.destinationPath,
            ExecutionItemStatus::Succeeded,
            {},
            QDateTime::currentDateTimeUtc(),
            false,
        },
        ExecutionItemResult{
            second,
            second.destinationPath,
            ExecutionItemStatus::SourceCleanupFailed,
            QStringLiteral("源文件清理失败"),
            QDateTime::currentDateTimeUtc(),
            false,
        },
    };

    ExecutionResultModel model;
    model.setResult(result);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.columnCount(), ExecutionResultModel::ColumnCount);
    QCOMPARE(
        model.data(model.index(0, ExecutionResultModel::SourcePath)).toString(),
        first.sourcePath);
    QCOMPARE(
        model.data(model.index(0, ExecutionResultModel::DestinationPath)).toString(),
        first.destinationPath);
    QCOMPARE(
        model.data(model.index(0, ExecutionResultModel::Status)).toString(),
        QStringLiteral("成功"));
    QCOMPARE(
        model.data(model.index(1, ExecutionResultModel::Status)).toString(),
        QStringLiteral("清理失败"));
    QCOMPARE(
        model.data(model.index(1, ExecutionResultModel::Message)).toString(),
        QStringLiteral("源文件清理失败"));
    QCOMPARE(
        model.headerData(ExecutionResultModel::Status, Qt::Horizontal).toString(),
        QStringLiteral("状态"));
}

void ExecutionResultModelTest::formatsCompleteSummaryWithoutMergingCategories()
{
    ExecutionSummary summary;
    summary.planned = 7;
    summary.succeeded = 3;
    summary.skipped = 1;
    summary.rejected = 1;
    summary.failed = 1;
    summary.sourceCleanupFailed = 1;
    summary.cancelled = 0;

    const QString text = ExecutionResultModel::summaryText(summary);
    QVERIFY(text.contains(QStringLiteral("处理：7")));
    QVERIFY(text.contains(QStringLiteral("成功：3")));
    QVERIFY(text.contains(QStringLiteral("跳过：1")));
    QVERIFY(text.contains(QStringLiteral("拒绝：1")));
    QVERIFY(text.contains(QStringLiteral("失败：1")));
    QVERIFY(text.contains(QStringLiteral("清理失败：1")));
    QVERIFY(text.contains(QStringLiteral("取消：0")));
}
namespace {

ExecutionItemResult historyItem(
    const QString &source,
    const QString &destination,
    const ExecutionItemStatus status,
    const QString &message = {},
    const bool resumed = false)
{
    OrganizePlanItem item;
    item.sourcePath = source;
    item.destinationPath = destination;
    return ExecutionItemResult{
        item,
        destination,
        status,
        message,
        QDateTime::currentDateTimeUtc(),
        resumed,
    };
}

ExecutionHistoryRepository makeHistoryRepository(
    const QString &path,
    const QString &label)
{
    return ExecutionHistoryRepository(
        path,
        QStringLiteral("FilePilotTestHistory_%1_%2")
            .arg(label)
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
}

} // namespace

void ExecutionHistoryRepositoryTest::initializesSchemaAndSavesCompleteResult()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath =
        QDir(directory.path()).filePath(QStringLiteral("history.sqlite"));
    ExecutionHistoryRepository repository(databasePath);
    QString error;
    QVERIFY2(repository.initialize(&error), qPrintable(error));

    const ExecutionContext context{
        QStringLiteral("context-1"),
        QStringLiteral("D:/target"),
        QStringLiteral("C:/scan"),
        11,
        17,
    };
    ExecutionResult result;
    result.completed = true;
    result.summary.planned = 3;
    result.summary.succeeded = 1;
    result.summary.rejected = 1;
    result.summary.sourceCleanupFailed = 1;
    result.items = {
        historyItem(
            QStringLiteral("C:/scan/first.txt"),
            QStringLiteral("D:/target/first.txt"),
            ExecutionItemStatus::Succeeded),
        historyItem(
            QStringLiteral("C:/scan/second.txt"),
            QStringLiteral("D:/target/second.txt"),
            ExecutionItemStatus::Rejected,
            QStringLiteral("目标路径无效")),
        historyItem(
            QStringLiteral("C:/scan/third.txt"),
            QStringLiteral("D:/target/third.txt"),
            ExecutionItemStatus::SourceCleanupFailed,
            QStringLiteral("源文件清理失败")),
    };

    qint64 historyId = 0;
    QVERIFY2(
        repository.saveExecutionResult(
            context, TaskState::CompletedWithErrors, result, &historyId, &error),
        qPrintable(error));
    QVERIFY(historyId > 0);

    const std::optional<ExecutionHistoryDetail> detail =
        repository.getExecution(historyId, &error);
    QVERIFY2(detail.has_value(), qPrintable(error));
    QCOMPARE(detail->record.executionId.size(), 36);
    QCOMPARE(detail->record.sourceRoot, context.scanSourceRoot);
    QCOMPARE(detail->record.targetRoot, context.targetRoot);
    QCOMPARE(detail->record.finalState, QStringLiteral("Completed with errors"));
    QCOMPARE(detail->record.summary.planned, 3);
    QCOMPARE(detail->record.summary.succeeded, 1);
    QCOMPARE(detail->record.summary.rejected, 1);
    QCOMPARE(detail->record.summary.failed, 0);
    QCOMPARE(detail->record.summary.sourceCleanupFailed, 1);
    QCOMPARE(detail->result.items.size(), std::size_t{3});
    QCOMPARE(detail->result.items.at(0).status, ExecutionItemStatus::Succeeded);
    QCOMPARE(detail->result.items.at(1).status, ExecutionItemStatus::Rejected);
    QCOMPARE(detail->result.items.at(2).status, ExecutionItemStatus::SourceCleanupFailed);
    QCOMPARE(detail->result.items.at(2).errorMessage, QStringLiteral("源文件清理失败"));
}

void ExecutionHistoryRepositoryTest::reopensAndDeletesHistoryWithItems()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath =
        QDir(directory.path()).filePath(QStringLiteral("history.sqlite"));
    const ExecutionContext context{
        QStringLiteral("context-2"),
        QStringLiteral("D:/target"),
        QStringLiteral("C:/scan"),
        1,
        1,
    };
    ExecutionResult result;
    result.completed = true;
    result.summary.planned = 1;
    result.summary.succeeded = 1;
    result.items = {
        historyItem(
            QStringLiteral("C:/scan/file.txt"),
            QStringLiteral("D:/target/file.txt"),
            ExecutionItemStatus::Succeeded),
    };

    qint64 historyId = 0;
    {
        ExecutionHistoryRepository repository =
            makeHistoryRepository(databasePath, QStringLiteral("save"));
        QString error;
        QVERIFY2(repository.initialize(&error), qPrintable(error));
        QVERIFY2(
            repository.saveExecutionResult(
                context, TaskState::Completed, result, &historyId, &error),
            qPrintable(error));
    }

    {
        ExecutionHistoryRepository repository =
            makeHistoryRepository(databasePath, QStringLiteral("reopen"));
        QString error;
        QVERIFY2(repository.initialize(&error), qPrintable(error));
        const auto records = repository.listExecutions(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(records.size(), std::size_t{1});
        QVERIFY(repository.getExecution(historyId, &error).has_value());
        QVERIFY2(repository.deleteExecution(historyId, &error), qPrintable(error));
    }

    {
        ExecutionHistoryRepository repository =
            makeHistoryRepository(databasePath, QStringLiteral("empty"));
        QString error;
        QVERIFY2(repository.initialize(&error), qPrintable(error));
        QCOMPARE(repository.listExecutions(&error).size(), std::size_t{0});
        QVERIFY(!repository.getExecution(historyId, &error).has_value());
    }
}

void ExecutionHistoryRepositoryTest::persistsFinalStatesAndCleanupSemantics()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ExecutionHistoryRepository repository =
        makeHistoryRepository(
            QDir(directory.path()).filePath(QStringLiteral("history.sqlite")),
            QStringLiteral("states"));
    QString error;
    QVERIFY2(repository.initialize(&error), qPrintable(error));

    const ExecutionContext context{
        QStringLiteral("context-states"),
        QStringLiteral("D:/target"),
        QStringLiteral("C:/scan"),
        1,
        1,
    };

    ExecutionResult completed;
    completed.completed = true;
    completed.summary.planned = 1;
    completed.summary.succeeded = 1;
    completed.items = {
        historyItem(
            QStringLiteral("C:/scan/completed.txt"),
            QStringLiteral("D:/target/completed.txt"),
            ExecutionItemStatus::Succeeded),
    };
    QVERIFY(repository.saveExecutionResult(
        context, TaskState::Completed, completed, nullptr, &error));

    ExecutionResult partial;
    partial.completed = true;
    partial.summary.planned = 2;
    partial.summary.succeeded = 1;
    partial.summary.rejected = 1;
    partial.items = {
        historyItem(
            QStringLiteral("C:/scan/ok.txt"),
            QStringLiteral("D:/target/ok.txt"),
            ExecutionItemStatus::Succeeded),
        historyItem(
            QStringLiteral("C:/scan/rejected.txt"),
            QStringLiteral("D:/target/rejected.txt"),
            ExecutionItemStatus::Rejected,
            QStringLiteral("拒绝")),
    };
    QVERIFY(repository.saveExecutionResult(
        context, TaskState::CompletedWithErrors, partial, nullptr, &error));

    ExecutionResult cancelled;
    cancelled.completed = true;
    cancelled.cancelled = true;
    cancelled.summary.planned = 1;
    cancelled.summary.cancelled = 1;
    cancelled.items = {
        historyItem(
            QStringLiteral("C:/scan/cancelled.txt"),
            QStringLiteral("D:/target/cancelled.txt"),
            ExecutionItemStatus::Cancelled,
            QStringLiteral("执行已取消")),
    };
    QVERIFY(repository.saveExecutionResult(
        context, TaskState::Cancelled, cancelled, nullptr, &error));

    ExecutionResult failed;
    failed.summary.planned = 1;
    failed.fatalError = QStringLiteral("任务异常");
    QVERIFY(repository.saveExecutionResult(
        context, TaskState::Failed, failed, nullptr, &error));

    ExecutionResult cleanup;
    cleanup.completed = true;
    cleanup.summary.planned = 1;
    cleanup.summary.sourceCleanupFailed = 1;
    cleanup.items = {
        historyItem(
            QStringLiteral("C:/scan/cleanup.txt"),
            QStringLiteral("D:/target/cleanup.txt"),
            ExecutionItemStatus::SourceCleanupFailed,
            QStringLiteral("源文件清理失败")),
    };
    QVERIFY(repository.saveExecutionResult(
        context, TaskState::CompletedWithErrors, cleanup, nullptr, &error));

    const auto records = repository.listExecutions(&error);
    QCOMPARE(records.size(), std::size_t{5});
    QSet<QString> states;
    for (const ExecutionHistoryRecord &record : records) {
        states.insert(record.finalState);
        if (record.summary.sourceCleanupFailed == 1) {
            QCOMPARE(record.summary.failed, 0);
            QCOMPARE(record.summary.sourceCleanupFailed, 1);
        }
    }
    QVERIFY(states.contains(QStringLiteral("Completed")));
    QVERIFY(states.contains(QStringLiteral("Completed with errors")));
    QVERIFY(states.contains(QStringLiteral("Cancelled")));
    QVERIFY(states.contains(QStringLiteral("Failed")));
}

void ExecutionHistoryRepositoryTest::databaseFailureDoesNotModifyExecutionResult()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString databasePath =
        QDir(directory.path()).filePath(QStringLiteral("missing/history.sqlite"));
    ExecutionHistoryRepository repository(databasePath);
    QString error;
    QVERIFY(!repository.initialize(&error));
    QVERIFY(!error.isEmpty());

    ExecutionResult result;
    result.completed = true;
    result.summary.planned = 1;
    result.summary.succeeded = 1;
    result.items = {
        historyItem(
            QStringLiteral("C:/scan/file.txt"),
            QStringLiteral("D:/target/file.txt"),
            ExecutionItemStatus::Succeeded),
    };
    const ExecutionResult before = result;
    const ExecutionContext context{
        QStringLiteral("context-db-failure"),
        QStringLiteral("D:/target"),
        QStringLiteral("C:/scan"),
        1,
        1,
    };
    QVERIFY(!repository.saveExecutionResult(
        context, TaskState::Completed, result, nullptr, &error));
    QCOMPARE(result.summary.planned, before.summary.planned);
    QCOMPARE(result.summary.succeeded, before.summary.succeeded);
    QCOMPARE(result.items.size(), before.items.size());
    QCOMPARE(result.items.at(0).status, before.items.at(0).status);
}

void HistoryPageTest::listsHistoryAndShowsSelectedDetails()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ExecutionHistoryRepository repository =
        makeHistoryRepository(
            QDir(directory.path()).filePath(QStringLiteral("history.sqlite")),
            QStringLiteral("page"));
    QString error;
    QVERIFY2(repository.initialize(&error), qPrintable(error));

    ExecutionResult result;
    result.completed = true;
    result.summary.planned = 2;
    result.summary.succeeded = 1;
    result.summary.rejected = 1;
    result.items = {
        historyItem(
            QStringLiteral("C:/scan/ok.txt"),
            QStringLiteral("D:/target/ok.txt"),
            ExecutionItemStatus::Succeeded),
        historyItem(
            QStringLiteral("C:/scan/rejected.txt"),
            QStringLiteral("D:/target/rejected.txt"),
            ExecutionItemStatus::Rejected,
            QStringLiteral("目标路径无效")),
    };
    const ExecutionContext context{
        QStringLiteral("context-page"),
        QStringLiteral("D:/target"),
        QStringLiteral("C:/scan"),
        1,
        1,
    };
    QVERIFY(repository.saveExecutionResult(
        context, TaskState::CompletedWithErrors, result, nullptr, &error));

    HistoryPage page(repository);
    auto *historyTable =
        page.findChild<QTableView *>(QStringLiteral("historyTableView"));
    auto *detailTable =
        page.findChild<QTableView *>(QStringLiteral("historyDetailTableView"));
    auto *deleteButton =
        page.findChild<QPushButton *>(QStringLiteral("deleteHistoryButton"));
    auto *statusLabel =
        page.findChild<QLabel *>(QStringLiteral("historyStatusLabel"));
    QVERIFY(historyTable != nullptr);
    QVERIFY(detailTable != nullptr);
    QVERIFY(deleteButton != nullptr);
    QVERIFY(statusLabel != nullptr);
    QCOMPARE(historyTable->model()->rowCount(), 1);
    QVERIFY(statusLabel->text().contains(QStringLiteral("共 1 条")));

    historyTable->selectionModel()->select(
        historyTable->model()->index(0, 0),
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    QTRY_COMPARE(detailTable->model()->rowCount(), 2);
    QVERIFY(deleteButton->isEnabled());

    QVERIFY(QMetaObject::invokeMethod(
        &page, "deleteSelectedExecution", Qt::DirectConnection));
    QCOMPARE(historyTable->model()->rowCount(), 0);
    QCOMPARE(detailTable->model()->rowCount(), 0);
}
namespace {

FileInfo duplicateFileInfo(const QString &path)
{
    const QFileInfo info(path);
    FileInfo file;
    file.absolutePath = path;
    file.fileName = info.fileName();
    file.extension = info.suffix().toLower();
    file.sizeBytes = info.size();
    file.createdUtc = info.birthTime().toUTC();
    file.modifiedUtc = info.lastModified().toUTC();
    file.kind = FileKind::RegularFile;
    return file;
}

ScanResult duplicateScanResult(const QStringList &paths)
{
    ScanResult scan;
    scan.completed = true;
    scan.rootPath = QStringLiteral("C:/duplicates");
    for (const QString &path : paths) {
        scan.files.push_back(duplicateFileInfo(path));
    }
    scan.statistics.fileCount = static_cast<qint64>(scan.files.size());
    return scan;
}

bool containsDuplicateFileSize(const DuplicateResult &result, const qint64 size)
{
    return std::any_of(
        result.groups.begin(),
        result.groups.end(),
        [size](const DuplicateGroup &group) {
            return group.fileSize == size;
        });
}

} // namespace

void DuplicateFinderTest::groupsExactDuplicatesAndCalculatesWastedSize()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();
    const QStringList paths{
        QDir(root).filePath(QStringLiteral("same-a.txt")),
        QDir(root).filePath(QStringLiteral("same-b.txt")),
        QDir(root).filePath(QStringLiteral("same-c.txt")),
        QDir(root).filePath(QStringLiteral("empty-a.txt")),
        QDir(root).filePath(QStringLiteral("empty-b.txt")),
        QDir(root).filePath(QStringLiteral("other-a.bin")),
        QDir(root).filePath(QStringLiteral("other-b.bin")),
    };
    QVERIFY(writeFile(paths.at(0), QByteArrayLiteral("same")));
    QVERIFY(writeFile(paths.at(1), QByteArrayLiteral("same")));
    QVERIFY(writeFile(paths.at(2), QByteArrayLiteral("same")));
    QVERIFY(writeFile(paths.at(3), QByteArray()));
    QVERIFY(writeFile(paths.at(4), QByteArray()));
    QVERIFY(writeFile(paths.at(5), QByteArrayLiteral("other")));
    QVERIFY(writeFile(paths.at(6), QByteArrayLiteral("other")));

    const DuplicateResult result =
        DuplicateFinder().findDuplicates(duplicateScanResult(paths), ScanCancellationToken{});
    QCOMPARE(result.state, TaskState::Completed);
    QCOMPARE(result.groups.size(), std::size_t{3});
    QVERIFY(containsDuplicateFileSize(result, 4));
    QVERIFY(containsDuplicateFileSize(result, 0));
    QVERIFY(containsDuplicateFileSize(result, 5));
    QCOMPARE(result.summary.groupCount, 3);
    QCOMPARE(result.summary.duplicateFileCount, 7);
    QCOMPARE(result.summary.duplicateBytes, 4 * 3 + 5 * 2);
    QCOMPARE(result.summary.wastedBytes, 4 * 2 + 5 * 1);
    QCOMPARE(result.summary.errorCount, 0);
}

void DuplicateFinderTest::rejectsDifferentContentAndPartialHashCollisions()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();
    const QString first = QDir(root).filePath(QStringLiteral("different-a.txt"));
    const QString second = QDir(root).filePath(QStringLiteral("different-b.txt"));
    QVERIFY(writeFile(first, QByteArrayLiteral("same-size-a")));
    QVERIFY(writeFile(second, QByteArrayLiteral("same-size-b")));

    const QString partialFirst =
        QDir(root).filePath(QStringLiteral("partial-a.bin"));
    const QString partialSecond =
        QDir(root).filePath(QStringLiteral("partial-b.bin"));
    QByteArray partialContent(256 * 1024, 'a');
    QVERIFY(writeFile(partialFirst, partialContent));
    partialContent[100 * 1024] = 'b';
    QVERIFY(writeFile(partialSecond, partialContent));

    const DuplicateResult result = DuplicateFinder().findDuplicates(
        duplicateScanResult({first, second, partialFirst, partialSecond}),
        ScanCancellationToken{});
    QVERIFY(result.groups.empty());
    QCOMPARE(result.summary.groupCount, 0);
    QCOMPARE(result.summary.candidateFiles, 4);
    QCOMPARE(result.summary.partialHashedFiles, 4);
    QCOMPARE(result.summary.fullHashedFiles, 2);
    QCOMPARE(result.summary.errorCount, 0);
    for (const DuplicateItem &item : result.items) {
        QVERIFY(item.status == DuplicateHashStatus::NotDuplicate);
    }
}

void DuplicateFinderTest::filtersDifferentSizesAndHandlesSmallAndLargeFiles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();
    const QString uniqueSmall = QDir(root).filePath(QStringLiteral("small.bin"));
    const QString uniqueLarge = QDir(root).filePath(QStringLiteral("large.bin"));
    QVERIFY(writeFile(uniqueSmall, QByteArrayLiteral("x")));
    QVERIFY(writeFile(uniqueLarge, QByteArray(1024 * 1024, 'z')));

    const QString smallA = QDir(root).filePath(QStringLiteral("small-a.txt"));
    const QString smallB = QDir(root).filePath(QStringLiteral("small-b.txt"));
    const QString largeA = QDir(root).filePath(QStringLiteral("large-a.bin"));
    const QString largeB = QDir(root).filePath(QStringLiteral("large-b.bin"));
    QVERIFY(writeFile(smallA, QByteArrayLiteral("small")));
    QVERIFY(writeFile(smallB, QByteArrayLiteral("small")));
    QVERIFY(writeFile(largeA, QByteArray(512 * 1024, 'q')));
    QVERIFY(writeFile(largeB, QByteArray(512 * 1024, 'q')));

    const DuplicateResult result = DuplicateFinder().findDuplicates(
        duplicateScanResult({
            uniqueSmall,
            uniqueLarge,
            smallA,
            smallB,
            largeA,
            largeB,
        }),
        ScanCancellationToken{});
    QCOMPARE(result.summary.totalFiles, 6);
    QCOMPARE(result.summary.candidateFiles, 4);
    QCOMPARE(result.summary.partialHashedFiles, 4);
    QCOMPARE(result.summary.fullHashedFiles, 4);
    QCOMPARE(result.summary.groupCount, 2);
    QVERIFY(containsDuplicateFileSize(result, 5));
    QVERIFY(containsDuplicateFileSize(result, 512 * 1024));
    QCOMPARE(result.summary.wastedBytes, 5 + 512 * 1024);
}

void DuplicateFinderTest::reportsMissingChangedAndCancelledFiles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();
    const QString validA = QDir(root).filePath(QStringLiteral("valid-a.txt"));
    const QString validB = QDir(root).filePath(QStringLiteral("valid-b.txt"));
    const QString missing = QDir(root).filePath(QStringLiteral("missing.txt"));
    const QString replaced = QDir(root).filePath(QStringLiteral("replaced.txt"));
    QVERIFY(writeFile(validA, QByteArrayLiteral("valid")));
    QVERIFY(writeFile(validB, QByteArrayLiteral("valid")));
    QVERIFY(writeFile(missing, QByteArrayLiteral("old")));
    QVERIFY(writeFile(replaced, QByteArrayLiteral("old")));

    const ScanResult scan =
        duplicateScanResult({validA, validB, missing, replaced});
    QVERIFY(QFile::remove(missing));
    QVERIFY(writeFile(replaced, QByteArrayLiteral("new-content")));

    ScanCancellationToken token;
    int progressCount = 0;
    const DuplicateResult result = DuplicateFinder().findDuplicates(
        scan,
        token,
        [&token, &progressCount](const DuplicateProgress &) {
            ++progressCount;
            if (progressCount == 1) {
                token.cancel();
            }
        });
    QVERIFY(result.cancelled);
    QCOMPARE(result.state, TaskState::Cancelled);
    QVERIFY(result.groups.empty());
    QCOMPARE(result.summary.errorCount, 0);

    const DuplicateResult withoutCancellation =
        DuplicateFinder().findDuplicates(scan, ScanCancellationToken{});
    QCOMPARE(withoutCancellation.groups.size(), std::size_t{1});
    QCOMPARE(withoutCancellation.summary.errorCount, 2);
    QCOMPARE(withoutCancellation.summary.duplicateFileCount, 2);
    bool sawFailed = false;
    bool sawChanged = false;
    for (const DuplicateItem &item : withoutCancellation.items) {
        sawFailed = sawFailed || item.status == DuplicateHashStatus::Failed;
        sawChanged = sawChanged || item.status == DuplicateHashStatus::Changed;
    }
    QVERIFY(sawFailed);
    QVERIFY(sawChanged);
}
void DuplicateFinderTest::computesSummaryAndErrorSemantics()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();
    const QString missingA = QDir(root).filePath(QStringLiteral("missing-a.txt"));
    const QString missingB = QDir(root).filePath(QStringLiteral("missing-b.txt"));
    const ScanResult scan = duplicateScanResult({missingA, missingB});

    std::vector<DuplicateError> callbackErrors;
    const DuplicateResult result = DuplicateFinder().findDuplicates(
        scan,
        ScanCancellationToken{},
        {},
        [&callbackErrors](const DuplicateError &error) {
            callbackErrors.push_back(error);
        });

    QVERIFY(result.completed);
    QCOMPARE(result.state, TaskState::CompletedWithErrors);
    QCOMPARE(result.summary.candidateFiles, 2);
    QCOMPARE(result.summary.processedFiles, 2);
    QCOMPARE(result.summary.errorCount, 2);
    QCOMPARE(result.errors.size(), std::size_t{2});
    QCOMPARE(callbackErrors.size(), std::size_t{2});
    QVERIFY(result.groups.empty());
}

void DuplicateTaskTest::runsInBackgroundWithProgress()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();
    const QString first = QDir(root).filePath(QStringLiteral("task-a.txt"));
    const QString second = QDir(root).filePath(QStringLiteral("task-b.txt"));
    QVERIFY(writeFile(first, QByteArrayLiteral("task-duplicate")));
    QVERIFY(writeFile(second, QByteArrayLiteral("task-duplicate")));

    DuplicateTask task;
    QSignalSpy completedSpy(&task, &DuplicateTask::completed);
    QSignalSpy progressSpy(&task, &DuplicateTask::progressChanged);
    QVERIFY(task.start(duplicateScanResult({first, second})));
    QTRY_COMPARE(completedSpy.count(), 1);
    QCOMPARE(task.state(), TaskState::Completed);
    QVERIFY(!progressSpy.isEmpty());
    const DuplicateResult result =
        completedSpy.at(0).at(0).value<DuplicateResult>();
    QCOMPARE(result.groups.size(), std::size_t{1});
    QCOMPARE(result.summary.duplicateFileCount, 2);
}

void DuplicateTaskTest::cancellationStopsBeforeNewHashes()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();
    const QString first = QDir(root).filePath(QStringLiteral("cancel-a.bin"));
    const QString second = QDir(root).filePath(QStringLiteral("cancel-b.bin"));
    const QByteArray content(1024 * 1024, 'c');
    QVERIFY(writeFile(first, content));
    QVERIFY(writeFile(second, content));

    DuplicateTask task;
    QSignalSpy completedSpy(&task, &DuplicateTask::completed);
    QSignalSpy cancelledSpy(&task, &DuplicateTask::cancelled);
    connect(
        &task,
        &DuplicateTask::progressChanged,
        &task,
        [&task](const DuplicateProgress &) { task.cancel(); },
        Qt::DirectConnection);
    QVERIFY(task.start(duplicateScanResult({first, second})));
    QTRY_VERIFY(isTerminalTaskState(task.state()));
    QTRY_VERIFY(completedSpy.count() == 1 || cancelledSpy.count() == 1);
    QCOMPARE(task.state(), TaskState::Cancelled);
}
void RuleEngineTest::usesDefaultCategories()
{
    const RuleEngine engine;
    QCOMPARE(engine.rules().size(), std::size_t{7});

    QCOMPARE(engine.classify(fileWithExtension(QStringLiteral("pdf"))),
             QStringLiteral("Documents"));
    QCOMPARE(engine.classify(fileWithExtension(QStringLiteral(".png"))),
             QStringLiteral("Images"));
    QCOMPARE(engine.classify(fileWithExtension(QStringLiteral("mp4"))),
             QStringLiteral("Videos"));
    QCOMPARE(engine.classify(fileWithExtension(QStringLiteral("mp3"))),
             QStringLiteral("Audio"));
    QCOMPARE(engine.classify(fileWithExtension(QStringLiteral("zip"))),
             QStringLiteral("Archives"));
    QCOMPARE(engine.classify(fileWithExtension(QStringLiteral("cpp"))),
             QStringLiteral("Programming"));
    QCOMPARE(engine.classify(fileWithExtension(QStringLiteral("unknown"))),
             QStringLiteral("Others"));
}

void RuleEngineTest::customRulesBeatOthersFallback()
{
    const RuleEngine engine({
        ClassificationRule{
            QStringLiteral("Fallback"),
            1,
            true,
            QStringLiteral("Others"),
            {},
            true,
        },
        ClassificationRule{
            QStringLiteral("Custom"),
            2000,
            true,
            QStringLiteral("Custom"),
            {QStringLiteral("pdf")},
            false,
        },
    });

    QCOMPARE(engine.classify(fileWithExtension(QStringLiteral("pdf"))),
             QStringLiteral("Custom"));
}

void RuleEngineTest::othersIsOnlyFinalFallback()
{
    const RuleEngine engine({
        ClassificationRule{
            QStringLiteral("OthersWildcard"),
            1,
            true,
            QStringLiteral("Others"),
            {},
            true,
        },
        ClassificationRule{
            QStringLiteral("Documents"),
            2,
            true,
            QStringLiteral("Documents"),
            {QStringLiteral("pdf")},
            false,
        },
    });

    QCOMPARE(engine.classify(fileWithExtension(QStringLiteral("pdf"))),
             QStringLiteral("Documents"));
    QCOMPARE(engine.classify(fileWithExtension(QStringLiteral("unknown"))),
             QStringLiteral("Others"));

    const RuleEngine disabledFallback({
        ClassificationRule{
            QStringLiteral("DisabledOthers"),
            1,
            false,
            QStringLiteral("Others"),
            {},
            true,
        },
    });
    QCOMPARE(disabledFallback.classify(fileWithExtension(QStringLiteral("pdf"))),
             QStringLiteral("Others"));
}

void RuleEngineTest::rejectsEmptyCategoryRules()
{
    const RuleEngine engine({
        ClassificationRule{
            QStringLiteral("Invalid"),
            1,
            true,
            QStringLiteral("   "),
            {QStringLiteral("pdf")},
            false,
        },
        ClassificationRule{
            QStringLiteral("Documents"),
            2,
            true,
            QStringLiteral("Documents"),
            {QStringLiteral("png")},
            false,
        },
    });

    QCOMPARE(engine.rules().size(), std::size_t{1});
    QCOMPARE(engine.classify(fileWithExtension(QStringLiteral("pdf"))),
             QStringLiteral("Others"));
    QCOMPARE(engine.classify(fileWithExtension(QStringLiteral("png"))),
             QStringLiteral("Documents"));
}
void RuleEngineTest::usesCustomRules()
{
    const RuleEngine engine({
        ClassificationRule{
            QStringLiteral("Temporary"),
            50,
            true,
            QStringLiteral("Temporary"),
            {QStringLiteral(".TMP")},
            false,
        },
        ClassificationRule{
            QStringLiteral("Fallback"),
            1000,
            true,
            QStringLiteral("Others"),
            {},
            true,
        },
    });

    QCOMPARE(engine.classify(fileWithExtension(QStringLiteral("tMp"))),
             QStringLiteral("Temporary"));
    QCOMPARE(engine.classify(fileWithExtension(QStringLiteral("bin"))),
             QStringLiteral("Others"));
}

void RuleEngineTest::appliesPriorityOrder()
{
    const RuleEngine engine({
        ClassificationRule{
            QStringLiteral("LowerPriority"),
            20,
            true,
            QStringLiteral("LowerPriority"),
            {QStringLiteral("pdf")},
            false,
        },
        ClassificationRule{
            QStringLiteral("HigherPriority"),
            10,
            true,
            QStringLiteral("HigherPriority"),
            {QStringLiteral("pdf")},
            false,
        },
    });

    QCOMPARE(engine.rules().at(0).category, QStringLiteral("HigherPriority"));
    QCOMPARE(engine.classify(fileWithExtension(QStringLiteral("pdf"))),
             QStringLiteral("HigherPriority"));
}

void RuleEngineTest::ignoresDisabledRules()
{
    const RuleEngine engine({
        ClassificationRule{
            QStringLiteral("Disabled"),
            10,
            false,
            QStringLiteral("Disabled"),
            {QStringLiteral("pdf")},
            false,
        },
        ClassificationRule{
            QStringLiteral("Fallback"),
            1000,
            true,
            QStringLiteral("Others"),
            {},
            true,
        },
    });

    QCOMPARE(engine.classify(fileWithExtension(QStringLiteral("pdf"))),
             QStringLiteral("Others"));
}

void RuleEngineTest::normalizesExtensions()
{
    QCOMPARE(RuleEngine::normalizeExtension(QStringLiteral(".PDF")), QStringLiteral("pdf"));
    QCOMPARE(RuleEngine::normalizeExtension(QStringLiteral(" .PnG ")), QStringLiteral("png"));
    QCOMPARE(RuleEngine::normalizeExtension(QStringLiteral("..Cpp")), QStringLiteral("cpp"));
    QCOMPARE(RuleEngine::normalizeExtension(QStringLiteral("pDf")), QStringLiteral("pdf"));
    QCOMPARE(RuleEngine::normalizeExtension(QString()), QString());
    QCOMPARE(RuleEngine::normalizeExtension(QStringLiteral("   ")), QString());
    QCOMPARE(RuleEngine::normalizeExtension(QStringLiteral(".")), QString());
    QCOMPARE(RuleEngine::normalizeExtension(QStringLiteral("..")), QString());
    QCOMPARE(RuleEngine::normalizeExtension(QStringLiteral(" . . ")), QString());
    QCOMPARE(RuleEngine::normalizeExtension(QStringLiteral("report.pdf")), QStringLiteral("report.pdf"));

    const RuleEngine engine({
        ClassificationRule{
            QStringLiteral("Normalized"),
            10,
            true,
            QStringLiteral("Normalized"),
            {QStringLiteral(".PDF")},
            false,
        },
    });

    QCOMPARE(engine.classify(fileWithExtension(QStringLiteral("pDf"))),
             QStringLiteral("Normalized"));
}

void RuleEngineTest::writesCategoriesToScanResult()
{
    ScanResult result;
    result.files.push_back(fileWithExtension(QStringLiteral(".PDF")));
    result.files.push_back(fileWithExtension(QStringLiteral("PNG")));
    result.files.push_back(fileWithExtension(QString()));

    RuleEngine().classify(result);

    QCOMPARE(result.files.at(0).category, QStringLiteral("Documents"));
    QCOMPARE(result.files.at(1).category, QStringLiteral("Images"));
    QCOMPARE(result.files.at(2).category, QStringLiteral("Others"));
}
void ScanServiceTest::emptyDirectory()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const ScanService service;
    const ScanResult result = service.scan(directory.path());

    QVERIFY(result.completed);
    QVERIFY(!result.cancelled);
    QVERIFY(result.errors.empty());
    QCOMPARE(result.statistics.fileCount, 0);
    QCOMPARE(result.statistics.totalSizeBytes, 0);
    QVERIFY(result.statistics.extensionCounts.isEmpty());
}

void ScanServiceTest::scansFilesAndStatistics()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();

    QVERIFY(writeFile(QDir(root).filePath(QStringLiteral("a.txt")), QByteArrayLiteral("12345")));
    QVERIFY(writeFile(QDir(root).filePath(QStringLiteral("image.PNG")), QByteArrayLiteral("abcd")));
    QVERIFY(writeFile(QDir(root).filePath(QStringLiteral("empty.bin")), QByteArray()));
    QVERIFY(writeFile(
        QDir(root).filePath(QStringLiteral("level1/level2/report.pdf")),
        QByteArrayLiteral("xyz")));

    const ScanService service;
    const ScanResult result = service.scan(root);

    QVERIFY(result.completed);
    QCOMPARE(result.statistics.fileCount, 4);
    QCOMPARE(result.statistics.totalSizeBytes, 12);
    QCOMPARE(result.statistics.extensionCounts.value(QStringLiteral("txt")), 1);
    QCOMPARE(result.statistics.extensionCounts.value(QStringLiteral("png")), 1);
    QCOMPARE(result.statistics.extensionCounts.value(QStringLiteral("bin")), 1);
    QCOMPARE(result.statistics.extensionCounts.value(QStringLiteral("pdf")), 1);
    QCOMPARE(result.files.size(), std::size_t{4});
}

void ScanServiceTest::scansNestedUnicodeAndSpecialPaths()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();
    const QString nested = QDir(root).filePath(QStringLiteral("中文 目录 [测试] & #"));

    QVERIFY(writeFile(
        QDir(nested).filePath(QStringLiteral("报告 (最终).txt")),
        QByteArrayLiteral("中文内容")));
    QVERIFY(writeFile(
        QDir(nested).filePath(QStringLiteral("特殊 #[]()&%.bin")),
        QByteArrayLiteral("x")));

    const ScanService service;
    const ScanResult result = service.scan(root);

    QVERIFY(result.completed);
    QCOMPARE(result.statistics.fileCount, 2);
    QCOMPARE(result.statistics.totalSizeBytes, 13);
    QCOMPARE(result.statistics.errorCount, 0);

    bool foundChineseFile = false;
    bool foundSpecialFile = false;
    for (const FileInfo &file : result.files) {
        foundChineseFile |=
            file.fileName == QStringLiteral("报告 (最终).txt");
        foundSpecialFile |=
            file.fileName == QStringLiteral("特殊 #[]()&%.bin");
    }
    QVERIFY(foundChineseFile);
    QVERIFY(foundSpecialFile);
}

void ScanServiceTest::rejectsMissingDirectory()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString missing =
        QDir(directory.path()).filePath(QStringLiteral("does-not-exist"));

    const ScanService service;
    const ScanResult result = service.scan(missing);

    QVERIFY(!result.completed);
    QVERIFY(!result.cancelled);
    QCOMPARE(result.statistics.fileCount, 0);
    QCOMPARE(result.statistics.errorCount, 1);
    QVERIFY(!result.fatalError.isEmpty());
}

void ScanServiceTest::recordsFileAccessFailureWithoutStopping()
{
#ifdef Q_OS_WIN
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();

    QVERIFY(writeFile(
        QDir(root).filePath(QStringLiteral("good.txt")), QByteArrayLiteral("ok")));
    const QString badPath = QDir(root).filePath(QStringLiteral("unreadable."));
    QVERIFY(createTrailingDotFile(badPath, QByteArrayLiteral("bad")));

    const ScanService service;
    bool errorReported = false;
    const ScanResult result = service.scan(
        root,
        ScanCancellationToken{},
        {},
        [&errorReported](const ScanError &) { errorReported = true; });

    QVERIFY(result.completed);
    QVERIFY(errorReported);
    QVERIFY(result.statistics.errorCount >= 1);
    QVERIFY(!result.errors.empty());
    QCOMPARE(result.statistics.extensionCounts.value(QStringLiteral("txt")), 1);

    removeTrailingDotFile(badPath);
#else
    QSKIP("Windows-specific file access failure fixture");
#endif
}

void ScanServiceTest::entryErrorDoesNotStopSiblingScan()
{
#ifdef Q_OS_WIN
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();

    QVERIFY(writeFile(QDir(root).filePath(QStringLiteral("before.txt")), QByteArrayLiteral("before")));
    QVERIFY(writeFile(QDir(root).filePath(QStringLiteral("after.txt")), QByteArrayLiteral("after")));
    const QString badPath = QDir(root).filePath(QStringLiteral("bad-entry."));
    QVERIFY(createTrailingDotFile(badPath, QByteArrayLiteral("bad")));

    const ScanService service;
    const ScanResult result = service.scan(root);

    QVERIFY(result.completed);
    QCOMPARE(result.statistics.fileCount, 2);
    QVERIFY(result.statistics.errorCount >= 1);

    QStringList names;
    for (const FileInfo &file : result.files) {
        names << file.fileName;
    }
    QVERIFY(names.contains(QStringLiteral("before.txt")));
    QVERIFY(names.contains(QStringLiteral("after.txt")));

    removeTrailingDotFile(badPath);
#else
    QSKIP("Windows-specific entry error fixture");
#endif
}
void ScanServiceTest::respectsCancellation()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    ScanCancellationToken token;
    token.cancel();

    const ScanService service;
    const ScanResult result = service.scan(directory.path(), token);

    QVERIFY(result.cancelled);
    QVERIFY(!result.completed);
    QCOMPARE(result.statistics.fileCount, 0);
}

void SettingsServiceTest::storesValuesInIniFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString settingsPath =
        QDir(directory.path()).filePath(QStringLiteral("settings.ini"));
    const QByteArray geometry = QByteArrayLiteral("geometry-value");
    const QByteArray state = QByteArrayLiteral("state-value");

    {
        SettingsService settings(settingsPath);
        settings.setLastDirectory(QStringLiteral("D:/Downloads"));
        settings.setDefaultBackupDirectory(QStringLiteral("E:/Backup"));
        settings.setLogLevelValue(2);
        settings.setWindowGeometry(geometry);
        settings.setWindowState(state);
        settings.sync();
    }

    SettingsService reloaded(settingsPath);
    QCOMPARE(reloaded.lastDirectory(), QStringLiteral("D:/Downloads"));
    QCOMPARE(reloaded.defaultBackupDirectory(), QStringLiteral("E:/Backup"));
    QCOMPARE(reloaded.logLevelValue(), 2);
    QCOMPARE(reloaded.windowGeometry(), geometry);
    QCOMPARE(reloaded.windowState(), state);
}

void LogManagerTest::writesEnabledEntries()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString logPath =
        QDir(directory.path()).filePath(QStringLiteral("logs/test.log"));
    {
        LogManager manager(logPath, LogLevel::Info);
        QVERIFY(manager.isAvailable());
        QVERIFY(!manager.log(LogLevel::Debug,
                            QStringLiteral("Test"),
                            QStringLiteral("Ignored debug entry")));
        QVERIFY(manager.log(LogLevel::Info,
                           QStringLiteral("Test"),
                           QStringLiteral("Info entry")));
        QVERIFY(manager.log(LogLevel::Error,
                           QStringLiteral("Test"),
                           QStringLiteral("Error entry")));
        QCOMPARE(manager.entryCount(), 2);
        manager.flush();
    }

    QFile file(logPath);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QTextStream stream(&file);
    const QString contents = stream.readAll();
    QVERIFY(contents.contains(QStringLiteral("[INFO] Test: Info entry")));
    QVERIFY(contents.contains(QStringLiteral("[ERROR] Test: Error entry")));
    QVERIFY(!contents.contains(QStringLiteral("Ignored debug entry")));
}

void ScanServiceTest::reportsExactErrorCountWithBoundedDetails()
{
#ifdef Q_OS_WIN
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();

    QStringList badPaths;
    for (int index = 0; index < 200; ++index) {
        const QString path =
            QDir(root).filePath(QStringLiteral("bad-%1.").arg(index));
        QVERIFY(createTrailingDotFile(path, QByteArrayLiteral("bad")));
        badPaths << path;
    }

    const ScanService service;
    int callbackCount = 0;
    const ScanResult result = service.scan(
        root,
        ScanCancellationToken{},
        {},
        [&callbackCount](const ScanError &) { ++callbackCount; });

    QVERIFY(result.completed);
    QCOMPARE(result.statistics.errorCount, 200);
    QCOMPARE(callbackCount, 200);
    QVERIFY(result.errors.size() <= 64);

    for (const QString &path : badPaths) {
        removeTrailingDotFile(path);
    }
#else
    QSKIP("Windows-specific trailing-dot error fixture");
#endif
}

void ScanServiceTest::windowsOrdinaryDirectoryScan()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();

    QVERIFY(writeFile(
        QDir(root).filePath(QStringLiteral("nested/ordinary.txt")),
        QByteArrayLiteral("ordinary")));

    const ScanService service;
    const ScanResult result = service.scan(root);

    QVERIFY(result.completed);
    QCOMPARE(result.statistics.fileCount, 1);
    QCOMPARE(result.statistics.errorCount, 0);
}

void ScanServiceTest::windowsRootJunctionIsRejected()
{
#ifdef Q_OS_WIN
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString target = QDir(directory.path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(target));
    QVERIFY(writeFile(QDir(target).filePath(QStringLiteral("target.txt")), QByteArrayLiteral("x")));

    const QString junction = QDir(directory.path()).filePath(QStringLiteral("junction"));
    QVERIFY(createJunction(junction, target));

    const ScanService service;
    const ScanResult result = service.scan(junction);

    QVERIFY(!result.completed);
    QVERIFY(result.fatalError.contains(QStringLiteral("Junction"))
        || result.fatalError.contains(QStringLiteral("reparse")));
    QVERIFY(result.statistics.errorCount >= 1);
    QDir().rmdir(junction);
#else
    QSKIP("Windows Junction fixture");
#endif
}

void ScanServiceTest::windowsNestedJunctionIsSkipped()
{
#ifdef Q_OS_WIN
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = QDir(directory.path()).filePath(QStringLiteral("root"));
    const QString target = QDir(directory.path()).filePath(QStringLiteral("target"));
    QVERIFY(QDir().mkpath(root));
    QVERIFY(QDir().mkpath(target));
    QVERIFY(writeFile(QDir(root).filePath(QStringLiteral("ordinary.txt")), QByteArrayLiteral("x")));
    QVERIFY(writeFile(QDir(target).filePath(QStringLiteral("target.txt")), QByteArrayLiteral("x")));

    const QString junction = QDir(root).filePath(QStringLiteral("junction"));
    QVERIFY(createJunction(junction, target));

    const ScanService service;
    const ScanResult result = service.scan(root);

    QVERIFY(result.completed);
    QCOMPARE(result.statistics.fileCount, 1);
    for (const FileInfo &file : result.files) {
        QVERIFY(!file.absolutePath.contains(QStringLiteral("target.txt")));
    }
    QDir().rmdir(junction);
#else
    QSKIP("Windows Junction fixture");
#endif
}

void ScanServiceTest::windowsJunctionCycleIsSafe()
{
#ifdef Q_OS_WIN
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = QDir(directory.path()).filePath(QStringLiteral("root"));
    QVERIFY(QDir().mkpath(root));

    const QString junction = QDir(root).filePath(QStringLiteral("junction"));
    QVERIFY(createJunction(junction, root));

    const ScanService service;
    const ScanResult result = service.scan(root);

    QVERIFY(result.completed);
    QCOMPARE(result.statistics.fileCount, 0);
    QDir().rmdir(junction);
#else
    QSKIP("Windows Junction fixture");
#endif
}
void ScanTaskTest::runsWithoutBlocking()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();

    for (int index = 0; index < 1000; ++index) {
        QVERIFY(writeFile(
            QDir(root).filePath(QStringLiteral("file-%1.txt").arg(index)),
            QByteArrayLiteral("x")));
    }

    ScanTask task;
    QSignalSpy completedSpy(&task, &ScanTask::completed);

    QTimer timer;
    timer.setSingleShot(true);
    timer.setInterval(0);
    QSignalSpy timerSpy(&timer, &QTimer::timeout);
    timer.start();

    QVERIFY(task.start(root));
    QTRY_VERIFY(timerSpy.count() > 0);
    QTRY_COMPARE(completedSpy.count(), 1);
    QCOMPARE(task.state(), TaskState::Completed);
    QTRY_VERIFY(!task.isActive());
}

void ScanTaskTest::canBeCancelled()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();

    for (int index = 0; index < 2000; ++index) {
        QVERIFY(writeFile(
            QDir(root).filePath(QStringLiteral("cancel-%1.txt").arg(index)),
            QByteArrayLiteral("x")));
    }

    ScanTask task;
    QSignalSpy cancelledSpy(&task, &ScanTask::cancelled);
    connect(&task, &ScanTask::progressChanged, &task, [&task] {
        task.cancel();
    });

    QVERIFY(task.start(root));
    QTRY_COMPARE(cancelledSpy.count(), 1);
    QCOMPARE(task.state(), TaskState::Cancelled);
    QVERIFY(!task.isActive());
}
void FileTableModelTest::exposesRowsColumnsAndHeaders()
{
    FileTableModel model;
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.columnCount(), 5);
    QCOMPARE(model.headerData(0, Qt::Horizontal).toString(), QStringLiteral("文件名"));
    QCOMPARE(model.headerData(1, Qt::Horizontal).toString(), QStringLiteral("类型"));
    QCOMPARE(model.headerData(2, Qt::Horizontal).toString(), QStringLiteral("大小"));
    QCOMPARE(model.headerData(3, Qt::Horizontal).toString(), QStringLiteral("修改时间"));
    QCOMPARE(model.headerData(4, Qt::Horizontal).toString(), QStringLiteral("路径"));
}

void FileTableModelTest::formatsFileRows()
{
    FileInfo first;
    first.absolutePath = QStringLiteral("C:/Data/report.pdf");
    first.fileName = QStringLiteral("report.pdf");
    first.extension = QStringLiteral("pdf");
    first.sizeBytes = 1536;
    first.modifiedUtc = QDateTime::fromMSecsSinceEpoch(0, QTimeZone::UTC);

    FileInfo second;
    second.absolutePath = QStringLiteral("C:/Data/notes");
    second.fileName = QStringLiteral("notes");
    second.extension = QString();
    second.sizeBytes = 0;

    FileTableModel model;
    model.setFiles({first, second});

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0, 0)).toString(), QStringLiteral("report.pdf"));
    QCOMPARE(model.data(model.index(0, 1)).toString(), QStringLiteral("PDF 文件"));
    QCOMPARE(model.data(model.index(0, 2)).toString(), QStringLiteral("1.5 KB"));
    QCOMPARE(model.data(model.index(1, 1)).toString(), QStringLiteral("文件"));
    QCOMPARE(model.data(model.index(1, 4)).toString(), QStringLiteral("C:/Data/notes"));
    QCOMPARE(model.data(model.index(0, 4), Qt::ToolTipRole).toString(),
             QStringLiteral("C:/Data/report.pdf"));
    QCOMPARE(FileTableModel::formatFileSize(0), QStringLiteral("0 B"));
}
void FileOrganizePageTest::scansAndDisplaysResults()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();

    QVERIFY(writeFile(QDir(root).filePath(QStringLiteral("first.txt")), QByteArrayLiteral("first")));
    QVERIFY(writeFile(QDir(root).filePath(QStringLiteral("second.png")), QByteArrayLiteral("second")));

    FileOrganizePage page(testApplication());
    auto *directoryEdit = page.findChild<QLineEdit *>(QStringLiteral("directoryEdit"));
    auto *tableView = page.findChild<QTableView *>(QStringLiteral("fileTableView"));
    auto *statusLabel = page.findChild<QLabel *>(QStringLiteral("scanStatusLabel"));
    auto *fileCountLabel = page.findChild<QLabel *>(QStringLiteral("fileCountValueLabel"));
    auto *categoryStatsLabel = page.findChild<QLabel *>(QStringLiteral("categoryStatsLabel"));

    QVERIFY(directoryEdit != nullptr);
    QVERIFY(tableView != nullptr);
    QVERIFY(statusLabel != nullptr);
    QVERIFY(fileCountLabel != nullptr);
    QVERIFY(categoryStatsLabel != nullptr);

    directoryEdit->setText(root);
    QVERIFY(QMetaObject::invokeMethod(&page, "startScan"));

    QTRY_VERIFY(tableView->model()->rowCount() == 2);
    QTRY_VERIFY(statusLabel->text().contains(QStringLiteral("扫描完成")));
    QCOMPARE(fileCountLabel->text(), QStringLiteral("文件：2"));
    QVERIFY(categoryStatsLabel->text().contains(QStringLiteral("Documents")));
    QVERIFY(categoryStatsLabel->text().contains(QStringLiteral("Images")));
}

void FileOrganizePageTest::categorySummaryShowsAllCategories()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();

    const QStringList extensions{
        QStringLiteral("pdf"),
        QStringLiteral("png"),
        QStringLiteral("mp4"),
        QStringLiteral("mp3"),
        QStringLiteral("zip"),
        QStringLiteral("cpp"),
        QStringLiteral("xyz"),
    };
    for (int index = 0; index < extensions.size(); ++index) {
        QVERIFY(writeFile(
            QDir(root).filePath(QStringLiteral("file-%1.%2").arg(index).arg(extensions.at(index))),
            QByteArrayLiteral("x")));
    }

    FileOrganizePage page(testApplication());
    auto *directoryEdit = page.findChild<QLineEdit *>(QStringLiteral("directoryEdit"));
    auto *categoryStatsLabel = page.findChild<QLabel *>(QStringLiteral("categoryStatsLabel"));
    auto *tableView = page.findChild<QTableView *>(QStringLiteral("fileTableView"));
    QVERIFY(directoryEdit != nullptr);
    QVERIFY(categoryStatsLabel != nullptr);
    QVERIFY(tableView != nullptr);

    directoryEdit->setText(root);
    QVERIFY(QMetaObject::invokeMethod(&page, "startScan"));
    QTRY_VERIFY(tableView->model()->rowCount() == extensions.size());
    QTRY_VERIFY(categoryStatsLabel->text().contains(QStringLiteral("Documents")));
    QTRY_VERIFY(categoryStatsLabel->text().contains(QStringLiteral("Images")));
    QTRY_VERIFY(categoryStatsLabel->text().contains(QStringLiteral("Videos")));
    QTRY_VERIFY(categoryStatsLabel->text().contains(QStringLiteral("Audio")));
    QTRY_VERIFY(categoryStatsLabel->text().contains(QStringLiteral("Archives")));
    QTRY_VERIFY(categoryStatsLabel->text().contains(QStringLiteral("Programming")));
    QTRY_VERIFY(categoryStatsLabel->text().contains(QStringLiteral("Others")));
}
void FileOrganizePageTest::invalidatesPlanWhenTargetRootChanges()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();
    QVERIFY(writeFile(QDir(root).filePath(QStringLiteral("first.txt")), QByteArrayLiteral("first")));
    QVERIFY(writeFile(QDir(root).filePath(QStringLiteral("second.png")), QByteArrayLiteral("second")));

    FileOrganizePage page(testApplication());
    auto *directoryEdit = page.findChild<QLineEdit *>(QStringLiteral("directoryEdit"));
    auto *targetRootEdit = page.findChild<QLineEdit *>(QStringLiteral("targetRootEdit"));
    auto *previewTableView = page.findChild<QTableView *>(QStringLiteral("previewTableView"));
    auto *previewStatusLabel = page.findChild<QLabel *>(QStringLiteral("previewStatusLabel"));
    auto *confirmButton = page.findChild<QPushButton *>(QStringLiteral("confirmPlanButton"));
    QVERIFY(directoryEdit != nullptr);
    QVERIFY(targetRootEdit != nullptr);
    QVERIFY(previewTableView != nullptr);
    QVERIFY(previewStatusLabel != nullptr);
    QVERIFY(confirmButton != nullptr);

    directoryEdit->setText(root);
    QVERIFY(QMetaObject::invokeMethod(&page, "startScan"));
    QTRY_VERIFY(page.findChild<QLabel *>(QStringLiteral("scanStatusLabel"))->text().contains(QStringLiteral("扫描完成")));
    targetRootEdit->setText(QDir(root).filePath(QStringLiteral("Organized")));
    QVERIFY(QMetaObject::invokeMethod(&page, "generatePreview"));
    QTRY_COMPARE(previewTableView->model()->rowCount(), 2);
    QVERIFY(confirmButton->isEnabled());

    targetRootEdit->setText(QDir(root).filePath(QStringLiteral("Changed")));
    QTRY_COMPARE(previewTableView->model()->rowCount(), 0);
    QVERIFY(!confirmButton->isEnabled());
    QVERIFY(previewStatusLabel->text().contains(QStringLiteral("目标根目录已修改")));
}

void FileOrganizePageTest::invalidatesPlanWhenRescanning()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();
    QVERIFY(writeFile(QDir(root).filePath(QStringLiteral("first.txt")), QByteArrayLiteral("first")));
    QVERIFY(writeFile(QDir(root).filePath(QStringLiteral("second.png")), QByteArrayLiteral("second")));

    FileOrganizePage page(testApplication());
    auto *directoryEdit = page.findChild<QLineEdit *>(QStringLiteral("directoryEdit"));
    auto *targetRootEdit = page.findChild<QLineEdit *>(QStringLiteral("targetRootEdit"));
    auto *previewTableView = page.findChild<QTableView *>(QStringLiteral("previewTableView"));
    auto *fileTableView = page.findChild<QTableView *>(QStringLiteral("fileTableView"));
    auto *previewStatusLabel = page.findChild<QLabel *>(QStringLiteral("previewStatusLabel"));
    auto *confirmButton = page.findChild<QPushButton *>(QStringLiteral("confirmPlanButton"));
    QVERIFY(directoryEdit != nullptr);
    QVERIFY(targetRootEdit != nullptr);
    QVERIFY(previewTableView != nullptr);
    QVERIFY(fileTableView != nullptr);
    QVERIFY(previewStatusLabel != nullptr);
    QVERIFY(confirmButton != nullptr);

    directoryEdit->setText(root);
    QVERIFY(QMetaObject::invokeMethod(&page, "startScan"));
    QTRY_COMPARE(fileTableView->model()->rowCount(), 2);
    targetRootEdit->setText(QDir(root).filePath(QStringLiteral("Organized")));
    QVERIFY(QMetaObject::invokeMethod(&page, "generatePreview"));
    QTRY_COMPARE(previewTableView->model()->rowCount(), 2);

    QVERIFY(QMetaObject::invokeMethod(&page, "startScan"));
    QTRY_COMPARE(fileTableView->model()->rowCount(), 2);
    QTRY_COMPARE(previewTableView->model()->rowCount(), 0);
    QVERIFY(previewStatusLabel->text().contains(QStringLiteral("旧整理计划已失效")));
    QVERIFY(!confirmButton->isEnabled());
}

void FileOrganizePageTest::regeneratingPlanResetsConfirmation()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();
    QVERIFY(writeFile(QDir(root).filePath(QStringLiteral("first.txt")), QByteArrayLiteral("first")));
    QVERIFY(writeFile(QDir(root).filePath(QStringLiteral("second.png")), QByteArrayLiteral("second")));

    FileOrganizePage page(testApplication());
    auto *directoryEdit = page.findChild<QLineEdit *>(QStringLiteral("directoryEdit"));
    auto *targetRootEdit = page.findChild<QLineEdit *>(QStringLiteral("targetRootEdit"));
    auto *previewTableView = page.findChild<QTableView *>(QStringLiteral("previewTableView"));
    auto *previewStatusLabel = page.findChild<QLabel *>(QStringLiteral("previewStatusLabel"));
    auto *confirmButton = page.findChild<QPushButton *>(QStringLiteral("confirmPlanButton"));
    QVERIFY(directoryEdit != nullptr);
    QVERIFY(targetRootEdit != nullptr);
    QVERIFY(previewTableView != nullptr);
    QVERIFY(previewStatusLabel != nullptr);
    QVERIFY(confirmButton != nullptr);

    directoryEdit->setText(root);
    QVERIFY(QMetaObject::invokeMethod(&page, "startScan"));
    QTRY_VERIFY(page.findChild<QLabel *>(QStringLiteral("scanStatusLabel"))->text().contains(QStringLiteral("扫描完成")));
    targetRootEdit->setText(QDir(root).filePath(QStringLiteral("Organized")));
    QVERIFY(QMetaObject::invokeMethod(&page, "generatePreview"));
    QTRY_COMPARE(previewTableView->model()->rowCount(), 2);
    QVERIFY(QMetaObject::invokeMethod(&page, "confirmPlan"));
    QVERIFY(!confirmButton->isEnabled());

    QVERIFY(QMetaObject::invokeMethod(&page, "generatePreview"));
    QTRY_COMPARE(previewTableView->model()->rowCount(), 2);
    QVERIFY(confirmButton->isEnabled());
    QVERIFY(previewStatusLabel->text().contains(QStringLiteral("整理预览已生成")));
}
void FileOrganizePageTest::displaysNoOpPreview()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();
    QVERIFY(writeFile(
        QDir(root).filePath(QStringLiteral("Documents/same.txt")),
        QByteArrayLiteral("same")));

    FileOrganizePage page(testApplication());
    auto *directoryEdit = page.findChild<QLineEdit *>(QStringLiteral("directoryEdit"));
    auto *targetRootEdit = page.findChild<QLineEdit *>(QStringLiteral("targetRootEdit"));
    auto *previewTableView = page.findChild<QTableView *>(QStringLiteral("previewTableView"));
    auto *previewNoOpLabel = page.findChild<QLabel *>(QStringLiteral("previewNoOpLabel"));
    QVERIFY(directoryEdit != nullptr);
    QVERIFY(targetRootEdit != nullptr);
    QVERIFY(previewTableView != nullptr);
    QVERIFY(previewNoOpLabel != nullptr);

    directoryEdit->setText(root);
    QVERIFY(QMetaObject::invokeMethod(&page, "startScan"));
    QTRY_VERIFY(page.findChild<QLabel *>(QStringLiteral("scanStatusLabel"))->text().contains(QStringLiteral("扫描完成")));
    targetRootEdit->setText(root);
    QVERIFY(QMetaObject::invokeMethod(&page, "generatePreview"));
    QTRY_COMPARE(previewTableView->model()->rowCount(), 1);
    QCOMPARE(previewNoOpLabel->text(), QStringLiteral("NoOp：1"));
    QCOMPARE(previewTableView->model()->data(
        previewTableView->model()->index(0, 4)).toString(),
        QStringLiteral("NoOp"));
}
void FileOrganizePageTest::reportsInvalidDirectory()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString missing = QDir(directory.path()).filePath(QStringLiteral("missing"));

    FileOrganizePage page(testApplication());
    auto *directoryEdit = page.findChild<QLineEdit *>(QStringLiteral("directoryEdit"));
    auto *statusLabel = page.findChild<QLabel *>(QStringLiteral("scanStatusLabel"));
    QVERIFY(directoryEdit != nullptr);
    QVERIFY(statusLabel != nullptr);

    directoryEdit->setText(missing);
    QVERIFY(QMetaObject::invokeMethod(&page, "startScan"));
    QTRY_VERIFY(statusLabel->text().contains(QStringLiteral("扫描失败")));
}
void ScanTaskTest::preparingCancellationIsStable()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();
    for (int index = 0; index < 3000; ++index) {
        QVERIFY(writeFile(
            QDir(root).filePath(QStringLiteral("prepare-%1.txt").arg(index)),
            QByteArrayLiteral("x")));
    }

    ScanTask task;
    QSignalSpy cancelledSpy(&task, &ScanTask::cancelled);
    QVERIFY(task.start(root));
    task.cancel();
    task.cancel();

    QTRY_VERIFY(isTerminalTaskState(task.state()));
    QCOMPARE(task.state(), TaskState::Cancelled);
    QTRY_COMPARE(cancelledSpy.count(), 1);
    QVERIFY(!task.isActive());
}

void ScanTaskTest::runningCancellationIsStable()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();
    for (int index = 0; index < 20000; ++index) {
        QVERIFY(writeFile(
            QDir(root).filePath(QStringLiteral("running-%1.txt").arg(index)),
            QByteArrayLiteral("x")));
    }

    ScanTask task;
    QSignalSpy cancelledSpy(&task, &ScanTask::cancelled);
    QVERIFY(task.start(root));
    QTRY_VERIFY(task.state() == TaskState::Running);
    task.cancel();
    task.cancel();

    QTRY_VERIFY(isTerminalTaskState(task.state()));
    QCOMPARE(task.state(), TaskState::Cancelled);
    QTRY_COMPARE(cancelledSpy.count(), 1);
}

void ScanTaskTest::completionAndCancellationRaceIsStable()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();
    QVERIFY(writeFile(QDir(root).filePath(QStringLiteral("race.txt")), QByteArrayLiteral("x")));

    ScanTask task;
    QSignalSpy completedSpy(&task, &ScanTask::completed);
    QSignalSpy cancelledSpy(&task, &ScanTask::cancelled);
    QVERIFY(task.start(root));
    task.cancel();

    QTRY_VERIFY(isTerminalTaskState(task.state()));
    const TaskState terminalState = task.state();
    QVERIFY(terminalState == TaskState::Cancelled
        || terminalState == TaskState::Completed
        || terminalState == TaskState::CompletedWithErrors);
    task.cancel();
    QCOMPARE(task.state(), terminalState);
    QVERIFY(completedSpy.count() + cancelledSpy.count() == 1);
    QVERIFY(!task.isActive());
}

void ScanTaskTest::repeatedCancelAndStartAreStable()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();
    QVERIFY(writeFile(QDir(root).filePath(QStringLiteral("repeat.txt")), QByteArrayLiteral("x")));

    ScanTask task;
    QVERIFY(task.start(root));
    QVERIFY(!task.start(root));
    task.cancel();
    task.cancel();
    QTRY_VERIFY(isTerminalTaskState(task.state()));

    const TaskState firstTerminalState = task.state();
    task.cancel();
    QCOMPARE(task.state(), firstTerminalState);

    QVERIFY(task.start(root));
    QTRY_VERIFY(isTerminalTaskState(task.state()));
    QVERIFY(isTerminalTaskState(task.state()));
    task.cancel();
    QVERIFY(isTerminalTaskState(task.state()));
    QVERIFY(!task.isActive());
}

void ScanTaskTest::flushesErrorBatchesWithoutDroppingCounts()
{
#ifdef Q_OS_WIN
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();

    QStringList badPaths;
    for (int index = 0; index < 120; ++index) {
        const QString path =
            QDir(root).filePath(QStringLiteral("batch-error-%1.").arg(index));
        QVERIFY(createTrailingDotFile(path, QByteArrayLiteral("bad")));
        badPaths << path;
    }

    ScanTask task;
    QList<ScanErrorBatch> batches;
    QSignalSpy completedSpy(&task, &ScanTask::completed);
    connect(&task, &ScanTask::errorBatchReported, &task,
            [&batches](const ScanErrorBatch batch) { batches.append(batch); });

    QVERIFY(task.start(root));
    QTRY_VERIFY(isTerminalTaskState(task.state()));
    QTRY_COMPARE(completedSpy.count(), 1);

    const ScanResult result =
        completedSpy.at(0).at(0).value<ScanResult>();
    QVERIFY(!batches.isEmpty());
    QCOMPARE(batches.constLast().totalErrorCount, result.statistics.errorCount);
    QCOMPARE(result.statistics.errorCount, 120);
    QVERIFY(result.errors.size() <= 64);

    for (const QString &path : badPaths) {
        removeTrailingDotFile(path);
    }
#else
    QSKIP("Windows-specific error batching fixture");
#endif
}
void FileOrganizePageTest::generatesPreviewAndConfirmsWithoutFilesystemChanges()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString root = directory.path();
    const QString firstPath = QDir(root).filePath(QStringLiteral("first.txt"));
    const QString secondPath = QDir(root).filePath(QStringLiteral("second.png"));
    QVERIFY(writeFile(firstPath, QByteArrayLiteral("first")));
    QVERIFY(writeFile(secondPath, QByteArrayLiteral("second")));

    const QString targetRoot = QDir(root).filePath(QStringLiteral("Organized"));
    QFile firstFile(firstPath);
    QVERIFY(firstFile.open(QIODevice::ReadOnly));
    const QByteArray firstBefore = firstFile.readAll();
    firstFile.close();
    QFile secondFile(secondPath);
    QVERIFY(secondFile.open(QIODevice::ReadOnly));
    const QByteArray secondBefore = secondFile.readAll();
    secondFile.close();

    FileOrganizePage page(testApplication());
    auto *directoryEdit = page.findChild<QLineEdit *>(QStringLiteral("directoryEdit"));
    auto *targetRootEdit = page.findChild<QLineEdit *>(QStringLiteral("targetRootEdit"));
    auto *previewTableView = page.findChild<QTableView *>(QStringLiteral("previewTableView"));
    auto *fileTableView = page.findChild<QTableView *>(QStringLiteral("fileTableView"));
    auto *previewTotalLabel = page.findChild<QLabel *>(QStringLiteral("previewTotalLabel"));
    auto *previewPlannedLabel = page.findChild<QLabel *>(QStringLiteral("previewPlannedLabel"));
    auto *previewInvalidLabel = page.findChild<QLabel *>(QStringLiteral("previewInvalidLabel"));
    auto *previewStatusLabel = page.findChild<QLabel *>(QStringLiteral("previewStatusLabel"));
    auto *confirmButton = page.findChild<QPushButton *>(QStringLiteral("confirmPlanButton"));
    auto *executionResultTable =
        page.findChild<QTableView *>(QStringLiteral("executionResultTableView"));
    auto *executionSummaryLabel =
        page.findChild<QLabel *>(QStringLiteral("executionSummaryLabel"));
    auto *executionProgressBar =
        page.findChild<QProgressBar *>(QStringLiteral("executionProgressBar"));
    auto *cancelButton = page.findChild<QPushButton *>(QStringLiteral("cancelPlanButton"));

    QVERIFY(directoryEdit != nullptr);
    QVERIFY(targetRootEdit != nullptr);
    QVERIFY(previewTableView != nullptr);
    QVERIFY(fileTableView != nullptr);
    QVERIFY(previewTotalLabel != nullptr);
    QVERIFY(previewPlannedLabel != nullptr);
    QVERIFY(previewInvalidLabel != nullptr);
    QVERIFY(previewStatusLabel != nullptr);
    QVERIFY(confirmButton != nullptr);
    QVERIFY(cancelButton != nullptr);
    QVERIFY(executionResultTable != nullptr);
    QVERIFY(executionSummaryLabel != nullptr);
    QVERIFY(executionProgressBar != nullptr);

    directoryEdit->setText(root);
    QVERIFY(QMetaObject::invokeMethod(&page, "startScan"));
    QTRY_VERIFY(fileTableView->model()->rowCount() == 2);

    targetRootEdit->setText(targetRoot);
    QVERIFY(QMetaObject::invokeMethod(&page, "generatePreview"));
    QTRY_COMPARE(previewTableView->model()->rowCount(), 2);
    QCOMPARE(previewTotalLabel->text(), QStringLiteral("总计划：2"));
    QCOMPARE(previewPlannedLabel->text(), QStringLiteral("Planned：2"));
    QCOMPARE(previewInvalidLabel->text(), QStringLiteral("Invalid：0"));
    QVERIFY(confirmButton->isEnabled());

    QVERIFY(QMetaObject::invokeMethod(&page, "confirmPlan"));
    QVERIFY(previewStatusLabel->text().contains(
        QStringLiteral("整理计划已确认，实际文件操作将在后续版本执行。")));
    QVERIFY(!confirmButton->isEnabled());
    QVERIFY(cancelButton->isEnabled());

    QFile firstAfter(firstPath);
    QVERIFY(firstAfter.open(QIODevice::ReadOnly));
    QCOMPARE(firstAfter.readAll(), firstBefore);
    firstAfter.close();
    QFile secondAfter(secondPath);
    QVERIFY(secondAfter.open(QIODevice::ReadOnly));
    QCOMPARE(secondAfter.readAll(), secondBefore);
    secondAfter.close();
    QVERIFY(QFile::exists(firstPath));
    QVERIFY(QFile::exists(secondPath));
        QVERIFY(!QFileInfo(QDir(targetRoot).filePath(QStringLiteral("Documents/first.txt"))).exists());
    QVERIFY(!QFileInfo(QDir(targetRoot).filePath(QStringLiteral("Images/second.png"))).exists());

    QVERIFY(QDir().mkpath(targetRoot));
    const auto historyBefore =
        testApplication().historyRepository().listExecutions();
    QVERIFY(QMetaObject::invokeMethod(&page, "startExecution"));
    QTRY_VERIFY(previewStatusLabel->text().contains(QStringLiteral("整理执行完成"))
        || previewStatusLabel->text().contains(QStringLiteral("整理执行失败"))
        || previewStatusLabel->text().contains(QStringLiteral("整理执行已取消")));
QVERIFY(!QFile::exists(firstPath));
    QVERIFY(!QFile::exists(secondPath));
    QVERIFY(QFileInfo(QDir(targetRoot).filePath(QStringLiteral("Documents/first.txt"))).exists());
    QVERIFY(QFileInfo(QDir(targetRoot).filePath(QStringLiteral("Images/second.png"))).exists());
    QCOMPARE(executionResultTable->model()->rowCount(), 2);
    QVERIFY(executionSummaryLabel->text().contains(QStringLiteral("处理：2")));
    QVERIFY(executionSummaryLabel->text().contains(QStringLiteral("成功：2")));
    QVERIFY(executionSummaryLabel->text().contains(QStringLiteral("失败：0")));
    QCOMPARE(executionProgressBar->maximum(), 2);
    QCOMPARE(executionProgressBar->value(), 2);
    const auto historyAfter =
        testApplication().historyRepository().listExecutions();
    QCOMPARE(historyAfter.size(), historyBefore.size() + 1);
    QVERIFY(std::any_of(
        historyAfter.begin(),
        historyAfter.end(),
        [&root](const ExecutionHistoryRecord &record) {
            return record.sourceRoot == root;
        }));

    QVERIFY(QMetaObject::invokeMethod(&page, "cancelPlan"));
    QTRY_COMPARE(previewTableView->model()->rowCount(), 0);
    QVERIFY(previewStatusLabel->text().contains(QStringLiteral("整理计划已取消")));
    QVERIFY(!confirmButton->isEnabled());
    QVERIFY(!cancelButton->isEnabled());
}
void FileOrganizePageTest::mapsExecutionResultStatesToUi()
{
    FileOrganizePage page(testApplication());
    auto *summaryLabel =
        page.findChild<QLabel *>(QStringLiteral("executionSummaryLabel"));
    auto *currentLabel =
        page.findChild<QLabel *>(QStringLiteral("executionCurrentFileLabel"));
    auto *progressBar =
        page.findChild<QProgressBar *>(QStringLiteral("executionProgressBar"));
    auto *resultTable =
        page.findChild<QTableView *>(QStringLiteral("executionResultTableView"));
    auto *statusLabel =
        page.findChild<QLabel *>(QStringLiteral("previewStatusLabel"));
    QVERIFY(summaryLabel != nullptr);
    QVERIFY(currentLabel != nullptr);
    QVERIFY(progressBar != nullptr);
    QVERIFY(resultTable != nullptr);
    QVERIFY(statusLabel != nullptr);

    OrganizePlanItem first;
    first.sourcePath = QStringLiteral("C:/source/first.txt");
    first.destinationPath = QStringLiteral("D:/target/first.txt");
    OrganizePlanItem second;
    second.sourcePath = QStringLiteral("C:/source/second.txt");
    second.destinationPath = QStringLiteral("D:/target/second.txt");

    ExecutionResult partial;
    partial.items = {
        ExecutionItemResult{
            first,
            first.destinationPath,
            ExecutionItemStatus::Succeeded,
            {},
            QDateTime::currentDateTimeUtc(),
            false,
        },
        ExecutionItemResult{
            second,
            second.destinationPath,
            ExecutionItemStatus::Rejected,
            QStringLiteral("目标路径无效"),
            QDateTime::currentDateTimeUtc(),
            false,
        },
    };
    partial.summary.planned = 3;
    partial.summary.succeeded = 1;
    partial.summary.rejected = 1;
    partial.summary.failed = 1;

    QVERIFY(QMetaObject::invokeMethod(
        &page,
        "handleExecutionResult",
        Qt::DirectConnection,
        Q_ARG(FilePilot::ExecutionResult, partial)));
    QCOMPARE(statusLabel->text(), QStringLiteral("整理执行部分完成"));
    QVERIFY(summaryLabel->text().contains(QStringLiteral("处理：3")));
    QVERIFY(summaryLabel->text().contains(QStringLiteral("成功：1")));
    QVERIFY(summaryLabel->text().contains(QStringLiteral("拒绝：1")));
    QVERIFY(summaryLabel->text().contains(QStringLiteral("失败：1")));
    QVERIFY(!summaryLabel->text().contains(QStringLiteral("失败：2")));
    QCOMPARE(progressBar->maximum(), 3);
    QCOMPARE(progressBar->value(), 2);
    QCOMPARE(resultTable->model()->rowCount(), 2);
    QCOMPARE(currentLabel->text(), QStringLiteral("当前项：执行结束"));

    ExecutionResult cleanupFailure;
    cleanupFailure.items = {
        ExecutionItemResult{
            first,
            first.destinationPath,
            ExecutionItemStatus::SourceCleanupFailed,
            QStringLiteral("源文件清理失败"),
            QDateTime::currentDateTimeUtc(),
            false,
        },
    };
    cleanupFailure.summary.planned = 1;
    cleanupFailure.summary.sourceCleanupFailed = 1;
    QVERIFY(QMetaObject::invokeMethod(
        &page,
        "handleExecutionResult",
        Qt::DirectConnection,
        Q_ARG(FilePilot::ExecutionResult, cleanupFailure)));
    QVERIFY(statusLabel->text().contains(QStringLiteral("源文件清理失败")));
    QVERIFY(summaryLabel->text().contains(QStringLiteral("清理失败：1")));
    QVERIFY(summaryLabel->text().contains(QStringLiteral("失败：0")));

    QVERIFY(QMetaObject::invokeMethod(
        &page,
        "handleExecutionState",
        Qt::DirectConnection,
        Q_ARG(FilePilot::TaskState, TaskState::Cancelling)));
    QVERIFY(statusLabel->text().contains(QStringLiteral("正在取消")));
    QVERIFY(statusLabel->text().contains(QStringLiteral("等待最终结果")));

    ExecutionResult cancelled;
    cancelled.items = {
        ExecutionItemResult{
            first,
            first.destinationPath,
            ExecutionItemStatus::Cancelled,
            QStringLiteral("执行已取消"),
            QDateTime::currentDateTimeUtc(),
            false,
        },
    };
    cancelled.summary.planned = 2;
    cancelled.summary.cancelled = 1;
    cancelled.cancelled = true;
    QVERIFY(QMetaObject::invokeMethod(
        &page,
        "handleExecutionResult",
        Qt::DirectConnection,
        Q_ARG(FilePilot::ExecutionResult, cancelled)));
    QCOMPARE(statusLabel->text(), QStringLiteral("整理执行已取消"));
    QCOMPARE(progressBar->maximum(), 2);
    QCOMPARE(progressBar->value(), 1);

    QVERIFY(QMetaObject::invokeMethod(
        &page,
        "handleExecutionFailure",
        Qt::DirectConnection,
        Q_ARG(QString, QStringLiteral("任务异常"))));
    QVERIFY(statusLabel->text().contains(QStringLiteral("整理执行失败")));
}
void MainWindowTest::buildsRequiredShell()
{
    MainWindow window(testApplication());
    QCOMPARE(window.windowTitle(), QStringLiteral("FilePilot"));
    QVERIFY(window.minimumWidth() >= 960);
    QVERIFY(window.minimumHeight() >= 640);
}

void MainWindowTest::switchesPagesThroughNavigation()
{
    MainWindow window(testApplication());

    auto *navigation = window.findChild<QListWidget *>(QStringLiteral("navigation"));
    auto *pageStack = window.findChild<QStackedWidget *>(QStringLiteral("pageStack"));
    auto *progressPanel = window.findChild<QWidget *>(QStringLiteral("globalProgressPanel"));
    auto *progressBar =
        window.findChild<QProgressBar *>(QStringLiteral("globalProgressBar"));
    auto *cancelButton =
        window.findChild<QPushButton *>(QStringLiteral("cancelTaskButton"));
    auto *toolBar = window.findChild<QToolBar *>(QStringLiteral("mainToolBar"));

    QVERIFY(navigation != nullptr);
    QVERIFY(pageStack != nullptr);
    QVERIFY(progressPanel != nullptr);
    QVERIFY(progressBar != nullptr);
    QVERIFY(cancelButton != nullptr);
    QVERIFY(toolBar != nullptr);

    QCOMPARE(navigation->count(), 5);
    QCOMPARE(pageStack->count(), 5);
    QCOMPARE(window.minimumSize(), QSize(960, 640));
    QVERIFY(!cancelButton->isEnabled());
    QCOMPARE(progressBar->value(), 0);

    const QStringList expectedPages{
        QStringLiteral("pageOrganize"),
        QStringLiteral("pageDuplicates"),
        QStringLiteral("pageBackup"),
        QStringLiteral("pageHistory"),
        QStringLiteral("pageSettings"),
    };

    for (int index = 0; index < expectedPages.size(); ++index) {
        navigation->setCurrentRow(index);
        QTRY_COMPARE(pageStack->currentWidget()->objectName(), expectedPages.at(index));
    }
}

void ThemePaletteTest::derivesLightAndDarkPalettes()
{
    const ThemePalette light = ThemePalette::fromSnapshot(
        themeSnapshot(ThemeMode::Light, QColor(QStringLiteral("#0078D4"))));
    const ThemePalette dark = ThemePalette::fromSnapshot(
        themeSnapshot(ThemeMode::Dark, QColor(QStringLiteral("#0078D4"))));

    QVERIFY(light.isValid());
    QVERIFY(dark.isValid());
    QVERIFY(light != dark);
    QCOMPARE(light.background(), QColor(QStringLiteral("#F3F3F3")));
    QCOMPARE(dark.background(), QColor(QStringLiteral("#202020")));
    QVERIFY(contrastRatio(light.textPrimary(), light.background()) >= 4.5);
    QVERIFY(contrastRatio(dark.textPrimary(), dark.background()) >= 4.5);
}

void ThemePaletteTest::derivesDefaultAndCustomAccents()
{
    const QList<QColor> accents{
        QColor(QStringLiteral("#0078D4")),
        QColor(QStringLiteral("#32CD32")),
        QColor(QStringLiteral("#7A3E9D")),
        QColor(QStringLiteral("#C42B1C")),
    };

    for (const QColor &accent : accents) {
        const ThemePalette palette = ThemePalette::fromSnapshot(
            themeSnapshot(ThemeMode::Light, accent));
        QCOMPARE(palette.accent(), accent);
        QVERIFY(palette.accentHover() != palette.accent());
        QVERIFY(palette.accentPressed() != palette.accent());
        QVERIFY(palette.accentSubtle() != palette.accent());
        QVERIFY(palette.selection() != palette.accent());
        QVERIFY(contrastRatio(palette.accentText(), palette.accent()) >= 4.5);
    }
}

void ThemePaletteTest::maintainsReadableAccentStatePairs()
{
    const QList<QColor> accents{
        QColor(QStringLiteral("#0078D4")),
        QColor(QStringLiteral("#008000")),
        QColor(QStringLiteral("#800080")),
        QColor(QStringLiteral("#FF0000")),
        QColor(QStringLiteral("#010101")),
        QColor(QStringLiteral("#FEFEFE")),
    };

    for (const ThemeMode mode : {ThemeMode::Light, ThemeMode::Dark}) {
        for (const QColor &accent : accents) {
            const ThemePalette palette = ThemePalette::fromSnapshot(
                themeSnapshot(mode, accent));

            QVERIFY(contrastRatio(palette.accentText(), palette.accent()) >= 4.5);
            QVERIFY(contrastRatio(
                palette.accentHoverText(),
                palette.accentHover()) >= 4.5);
            QVERIFY(contrastRatio(
                palette.accentPressedText(),
                palette.accentPressed()) >= 4.5);
            QVERIFY(contrastRatio(palette.selectionText(), palette.selection()) >= 4.5);

            QVERIFY(colorDistance(palette.accent(), palette.accentHover()) >= 12);
            QVERIFY(colorDistance(palette.accentHover(), palette.accentPressed()) >= 12);
            QVERIFY(palette.accent() != palette.accentPressed());
        }
    }
}

void ThemePaletteTest::usesHighContrastSystemSemantics()
{
    ThemeSnapshot snapshot = themeSnapshot(
        ThemeMode::Dark,
        QColor(QStringLiteral("#32CD32")),
        true);
    snapshot.background = QColor(QStringLiteral("#000000"));
    snapshot.foreground = QColor(QStringLiteral("#FFFFFF"));
    snapshot.highlight = QColor(QStringLiteral("#00A3FF"));
    snapshot.highlightText = QColor(QStringLiteral("#000000"));
    snapshot.buttonFace = QColor(QStringLiteral("#2D2D2D"));
    snapshot.buttonText = QColor(QStringLiteral("#FFFFFF"));
    snapshot.hotLight = QColor(QStringLiteral("#FFFF00"));
    snapshot.windowFrame = QColor(QStringLiteral("#FFFFFF"));
    snapshot.grayText = QColor(QStringLiteral("#A0A0A0"));

    const ThemePalette palette = ThemePalette::fromSnapshot(snapshot);
    QVERIFY(palette.highContrast());
    QCOMPARE(palette.background(), snapshot.background);
    QCOMPARE(palette.textPrimary(), snapshot.foreground);
    QCOMPARE(palette.surfaceSecondary(), snapshot.buttonFace);
    QCOMPARE(palette.textSecondary(), snapshot.buttonText);
    QCOMPARE(palette.selection(), snapshot.highlight);
    QCOMPARE(palette.selectionText(), snapshot.highlightText);
    QCOMPARE(palette.focus(), snapshot.hotLight);
    QCOMPARE(palette.border(), snapshot.windowFrame);
    QCOMPARE(palette.accent(), snapshot.highlight);
    QCOMPARE(palette.error(), snapshot.foreground);
}

void ThemePaletteTest::fallsBackWhenSnapshotIsInvalid()
{
    const ThemePalette light = ThemePalette::fromSnapshot(ThemeSnapshot{});
    ThemeSnapshot darkSnapshot;
    darkSnapshot.mode = ThemeMode::Dark;
    const ThemePalette dark = ThemePalette::fromSnapshot(darkSnapshot);

    QVERIFY(light.isValid());
    QVERIFY(dark.isValid());
    QCOMPARE(light.accent(), QColor(QStringLiteral("#0067C0")));
    QCOMPARE(light.background(), QColor(QStringLiteral("#F3F3F3")));
    QCOMPARE(dark.background(), QColor(QStringLiteral("#202020")));
}

void ThemePaletteTest::maintainsContrastRatios()
{
    const QList<QColor> accents{
        QColor(QStringLiteral("#0078D4")),
        QColor(QStringLiteral("#32CD32")),
        QColor(QStringLiteral("#7A3E9D")),
        QColor(QStringLiteral("#C42B1C")),
    };

    for (const ThemeMode mode : {ThemeMode::Light, ThemeMode::Dark}) {
        for (const QColor &accent : accents) {
            const ThemePalette palette =
                ThemePalette::fromSnapshot(themeSnapshot(mode, accent));
            QVERIFY(contrastRatio(palette.textPrimary(), palette.background()) >= 4.5);
            QVERIFY(contrastRatio(palette.accentText(), palette.accent()) >= 4.5);
            QVERIFY(contrastRatio(palette.selectionText(), palette.selection()) >= 4.5);
            QVERIFY(contrastRatio(palette.error(), palette.surface()) >= 4.5);
        }
    }
}

void ThemePaletteTest::usesAccessibleTooltipQss()
{
    const QList<QPair<ThemeMode, QColor>> cases{
        {ThemeMode::Light, QColor(QStringLiteral("#0078D4"))},
        {ThemeMode::Dark, QColor(QStringLiteral("#800080"))},
    };

    for (const auto &entry : cases) {
        const ThemePalette palette = ThemePalette::fromSnapshot(
            themeSnapshot(entry.first, entry.second));
        const QString styleSheet = fluentStyleSheet(palette);
        const QString body = qssRuleBody(styleSheet, QStringLiteral("QToolTip"));

        const QColor background(qssDeclarationValue(body, QStringLiteral("background")));
        const QColor foreground(qssDeclarationValue(body, QStringLiteral("color")));

        QCOMPARE(background, palette.surface());
        QCOMPARE(foreground, palette.textPrimary());
        QVERIFY(contrastRatio(foreground, background) >= 4.5);
    }
}

void ThemePaletteTest::usesValidFocusQss()
{
    ThemeSnapshot highContrast = highContrastSelectionSnapshot();
    highContrast.hotLight = QColor(QStringLiteral("#FFFF00"));

    const QList<ThemePalette> palettes{
        ThemePalette::fromSnapshot(
            themeSnapshot(ThemeMode::Light, QColor(QStringLiteral("#0078D4")))),
        ThemePalette::fromSnapshot(
            themeSnapshot(ThemeMode::Dark, QColor(QStringLiteral("#800080")))),
        ThemePalette::fromSnapshot(highContrast),
    };

    const QList<QPair<QString, QString>> focusRules{
        {QStringLiteral("QListWidget#navigation::item:focus"), QStringLiteral("outline")},
        {QStringLiteral("QLineEdit:focus"), QStringLiteral("border")},
        {QStringLiteral("QPushButton:focus"), QStringLiteral("border")},
    };

    for (const ThemePalette &palette : palettes) {
        const QString styleSheet = fluentStyleSheet(palette);
        QVERIFY(!styleSheet.contains(
            QRegularExpression(QStringLiteral("#[0-9A-Fa-f]{6}px"))));
        QVERIFY(!styleSheet.contains(QStringLiteral("pxpx")));
        QVERIFY(!styleSheet.contains(QRegularExpression(QStringLiteral("%\\d+"))));

        for (const auto &rule : focusRules) {
            const QString body = qssRuleBody(styleSheet, rule.first + QStringLiteral(" {"));
            const QString value = qssDeclarationValue(body, rule.second);
            const QString expected = QStringLiteral("2px solid ") + palette.focus().name();

            QCOMPARE(value, expected);
            QCOMPARE(QColor(value.section(QLatin1Char(' '), 2, 2)), palette.focus());
        }
    }
}

void ThemePaletteTest::usesSelectionTextForSelectedStates()
{
    const ThemeSnapshot snapshot = highContrastSelectionSnapshot();
    const ThemePalette palette = ThemePalette::fromSnapshot(snapshot);
    const QString styleSheet = fluentStyleSheet(palette);

    QVERIFY(snapshot.highlight != snapshot.foreground);
    QVERIFY(snapshot.highlightText != snapshot.foreground);

    const QString navigation = qssRuleBody(
        styleSheet,
        QStringLiteral("QListWidget#navigation::item:selected {"));
    const QString table = qssRuleBody(styleSheet, QStringLiteral("QTableView {"));
    const QString tableItem = qssRuleBody(
        styleSheet,
        QStringLiteral("QTableView::item:selected {"));
    const QString lineEdit = qssRuleBody(styleSheet, QStringLiteral("QLineEdit {"));

    QCOMPARE(
        qssDeclarationValue(navigation, QStringLiteral("color")),
        palette.selectionText().name());
    QCOMPARE(
        qssDeclarationValue(table, QStringLiteral("selection-color")),
        palette.selectionText().name());
    QCOMPARE(
        qssDeclarationValue(tableItem, QStringLiteral("color")),
        palette.selectionText().name());
    QCOMPARE(
        qssDeclarationValue(lineEdit, QStringLiteral("selection-background-color")),
        palette.selection().name());
    QCOMPARE(
        qssDeclarationValue(lineEdit, QStringLiteral("selection-color")),
        palette.selectionText().name());

    QVERIFY(palette.selectionText().name() != palette.textPrimary().name());
}

void ThemeDetectorTest::readsCurrentSnapshot()
{
    WindowsThemeDetector detector;
    const ThemeSnapshot snapshot = detector.snapshot();
    QVERIFY(snapshot.mode != ThemeMode::Unknown
            || snapshot.highContrast
            || snapshot.accent.isValid());
    QVERIFY(snapshot.baseThemeName.isEmpty());
    QVERIFY(snapshot.currentThemeType.isEmpty());
}

void ThemeDetectorTest::emitsOnlyChangedSnapshots()
{
    ThemeSnapshot current = themeSnapshot(
        ThemeMode::Light,
        QColor(QStringLiteral("#0078D4")));
    WindowsThemeDetector detector([&current] { return current; });
    QSignalSpy changedSpy(&detector, &WindowsThemeDetector::themeChanged);

    detector.start();
    QCOMPARE(changedSpy.count(), 1);
    detector.refreshNow();
    QCOMPARE(changedSpy.count(), 1);

    current.accent = QColor(QStringLiteral("#32CD32"));
    detector.refreshNow();
    QCOMPARE(changedSpy.count(), 2);
}

void ThemeDetectorTest::usesModeFallbackPriority()
{
    QCOMPARE(
        WindowsThemeDetector::resolveMode(true, false, ThemeMode::Dark),
        ThemeMode::Light);
    QCOMPARE(
        WindowsThemeDetector::resolveMode(false, true, ThemeMode::Light),
        ThemeMode::Dark);
    QCOMPARE(
        WindowsThemeDetector::resolveMode(std::nullopt, true, ThemeMode::Dark),
        ThemeMode::Light);
    QCOMPARE(
        WindowsThemeDetector::resolveMode(std::nullopt, false, ThemeMode::Light),
        ThemeMode::Dark);
    QCOMPARE(
        WindowsThemeDetector::resolveMode(
            std::nullopt,
            std::nullopt,
            ThemeMode::Dark),
        ThemeMode::Dark);
    QCOMPARE(
        WindowsThemeDetector::resolveMode(
            std::nullopt,
            std::nullopt,
            ThemeMode::Unknown),
        ThemeMode::Unknown);
}

void ThemeDetectorTest::switchesLightDarkAccentAndHighContrast()
{
    qRegisterMetaType<ThemeSnapshot>();
    ThemeSnapshot current = themeSnapshot(
        ThemeMode::Light,
        QColor(QStringLiteral("#0078D4")));
    WindowsThemeDetector detector([&current] { return current; });
    QSignalSpy changedSpy(&detector, &WindowsThemeDetector::themeChanged);

    detector.start();
    current = themeSnapshot(
        ThemeMode::Dark,
        QColor(QStringLiteral("#7A3E9D")));
    detector.refreshNow();
    current = themeSnapshot(
        ThemeMode::Light,
        QColor(QStringLiteral("#32CD32")));
    detector.refreshNow();
    current = themeSnapshot(
        ThemeMode::Light,
        QColor(QStringLiteral("#C42B1C")),
        true);
    detector.refreshNow();
    current = themeSnapshot(
        ThemeMode::Light,
        QColor(QStringLiteral("#32CD32")));
    detector.refreshNow();

    QCOMPARE(changedSpy.count(), 5);
    const auto dark = qvariant_cast<ThemeSnapshot>(changedSpy.at(1).at(0));
    const auto light = qvariant_cast<ThemeSnapshot>(changedSpy.at(2).at(0));
    const auto highContrast = qvariant_cast<ThemeSnapshot>(changedSpy.at(3).at(0));
    const auto highContrastOff = qvariant_cast<ThemeSnapshot>(changedSpy.at(4).at(0));
    QCOMPARE(dark.mode, ThemeMode::Dark);
    QCOMPARE(dark.accent, QColor(QStringLiteral("#7A3E9D")));
    QCOMPARE(light.mode, ThemeMode::Light);
    QCOMPARE(light.accent, QColor(QStringLiteral("#32CD32")));
    QVERIFY(!light.highContrast);
    QVERIFY(highContrast.highContrast);
    QCOMPARE(highContrast.accent, QColor(QStringLiteral("#C42B1C")));
    QVERIFY(!highContrastOff.highContrast);
}

void ThemeDetectorTest::usesFallbackForInvalidState()
{
    const ThemeSnapshot invalid;
    WindowsThemeDetector detector([&invalid] { return invalid; });
    QCOMPARE(detector.snapshot(), invalid);

    const ThemePalette palette = ThemePalette::fromSnapshot(invalid);
    QVERIFY(palette.isValid());
    QCOMPARE(palette.accent(), QColor(QStringLiteral("#0067C0")));
    QCOMPARE(palette.background(), QColor(QStringLiteral("#F3F3F3")));
}

void ThemeDetectorTest::remainsOnGuiThread()
{
    WindowsThemeDetector detector([] {
        return themeSnapshot(
            ThemeMode::Light,
            QColor(QStringLiteral("#0078D4")));
    });
    detector.start();
    detector.refreshNow();
    QCOMPARE(detector.thread(), QThread::currentThread());
    QCOMPARE(detector.thread(), QCoreApplication::instance()->thread());
}

void MainWindowThemeSmokeTest::appliesThemeToMainWindow()
{
    MainWindow window(testApplication());
    QVERIFY(!window.styleSheet().isEmpty());
    QVERIFY(window.palette().color(QPalette::Window).isValid());
    QVERIFY(window.palette().color(QPalette::Text).isValid());
    QVERIFY(window.findChild<QWidget *>(QStringLiteral("pageDuplicates")) != nullptr);
}

void MainWindowThemeSmokeTest::refreshesWithoutRecursivePaletteLoop()
{
    QWidget target;
    QtThemeApplier applier(&target);
    ThemeSnapshot current = themeSnapshot(
        ThemeMode::Light,
        QColor(QStringLiteral("#0078D4")));
    WindowsThemeDetector detector([&current] { return current; });
    QSignalSpy changedSpy(&detector, &WindowsThemeDetector::themeChanged);

    connect(
        &detector,
        &WindowsThemeDetector::themeChanged,
        &target,
        [&applier](const ThemeSnapshot &snapshot) {
            applier.apply(ThemePalette::fromSnapshot(snapshot));
        });

    detector.start();
    QCOMPARE(changedSpy.count(), 1);
    QVERIFY(!applier.isApplying());

    for (int index = 0; index < 5; ++index) {
        QEvent event(QEvent::ApplicationPaletteChange);
        QCoreApplication::sendEvent(QCoreApplication::instance(), &event);
    }
    QTest::qWait(100);
    QCOMPARE(changedSpy.count(), 1);
    QVERIFY(!applier.isApplying());

    current.mode = ThemeMode::Dark;
    detector.refreshNow();
    QCOMPARE(changedSpy.count(), 2);
    QVERIFY(!applier.isApplying());
}

void DpiLayoutTest::keepsShellWithinViewport()
{
    const qreal expectedScale = qEnvironmentVariableIsSet("QT_SCALE_FACTOR")
        ? QString::fromLocal8Bit(qgetenv("QT_SCALE_FACTOR")).toDouble()
        : 1.0;

    MainWindow window(testApplication());
    window.show();
    for (const QSize &size : {
             QSize(960, 640),
             QSize(1200, 800),
             QSize(1440, 900),
             QSize(1920, 1200),
         }) {
        window.resize(size);
        QCoreApplication::processEvents();
        QVERIFY(visibleChildrenFit(window));
    }

    QVERIFY(qAbs(window.devicePixelRatioF() - expectedScale) < 0.01);
    window.hide();
}

void DpiLayoutTest::keepsDuplicatePageWithinViewport()
{
    DuplicateFilesPage page;
    page.show();
    for (const QSize &size : {
             QSize(960, 640),
             QSize(1200, 800),
             QSize(1440, 900),
             QSize(1920, 1200),
         }) {
        page.resize(size);
        QCoreApplication::processEvents();
        QVERIFY(visibleChildrenFit(page));
    }
    page.hide();
}

void DpiLayoutTest::generatesLogicalSizedIcons()
{
    MainWindow window(testApplication());
    const QIcon icon = windowsGlyphIcon(&window, 0xE8DA, 18);
    if (icon.isNull()) {
        QSKIP("Segoe Fluent Icons is not available in the test environment.");
    }

    const QPixmap pixmap = icon.pixmap(QSize(18, 18));
    QVERIFY(!pixmap.isNull());
    QVERIFY(pixmap.width() >= 18);
    QVERIFY(pixmap.height() >= 18);
}


} // namespace Test
} // namespace FilePilot

bool shouldRunTestClass(const char *className)
{
    const QByteArray selected = qgetenv("FILEPILOT_TEST_CLASS");
    return selected.isEmpty() || selected == className;
}

template<typename TestClass>
int runTestClass(const char *className, int argc, char *argv[])
{
    if (!shouldRunTestClass(className)) {
        return 0;
    }

    TestClass test;
    return QTest::qExec(&test, argc, argv);
}

int main(int argc, char *argv[])
{
    QTemporaryDir applicationData;
    if (!applicationData.isValid()) {
        return 1;
    }

    FilePilot::Application application(argc, argv, applicationData.path());

    int status = 0;
    status |= runTestClass<FilePilot::Test::OrganizeExecutionPrevalidatorTest>(
        "OrganizeExecutionPrevalidatorTest", argc, argv);
    status |= runTestClass<FilePilot::Test::ConflictResolverTest>(
        "ConflictResolverTest", argc, argv);
    status |= runTestClass<FilePilot::Test::FileOperatorTest>(
        "FileOperatorTest", argc, argv);
    status |= runTestClass<FilePilot::Test::OrganizeExecutionTaskTest>(
        "OrganizeExecutionTaskTest", argc, argv);
    status |= runTestClass<FilePilot::Test::OrganizePlanTest>(
        "OrganizePlanTest", argc, argv);
    status |= runTestClass<FilePilot::Test::OrganizePlannerTest>(
        "OrganizePlannerTest", argc, argv);
    status |= runTestClass<FilePilot::Test::OrganizePreviewModelTest>(
        "OrganizePreviewModelTest", argc, argv);
    status |= runTestClass<FilePilot::Test::DuplicateFinderTest>(
        "DuplicateFinderTest", argc, argv);
    status |= runTestClass<FilePilot::Test::DuplicateTaskTest>(
        "DuplicateTaskTest", argc, argv);
    status |= runTestClass<FilePilot::Test::ExecutionHistoryRepositoryTest>(
        "ExecutionHistoryRepositoryTest", argc, argv);
    status |= runTestClass<FilePilot::Test::HistoryPageTest>(
        "HistoryPageTest", argc, argv);
    status |= runTestClass<FilePilot::Test::ExecutionResultModelTest>(
        "ExecutionResultModelTest", argc, argv);
    status |= runTestClass<FilePilot::Test::RuleEngineTest>(
        "RuleEngineTest", argc, argv);
    status |= runTestClass<FilePilot::Test::CoreModelTest>(
        "CoreModelTest", argc, argv);
    status |= runTestClass<FilePilot::Test::ScanServiceTest>(
        "ScanServiceTest", argc, argv);
    status |= runTestClass<FilePilot::Test::ScanTaskTest>(
        "ScanTaskTest", argc, argv);
    status |= runTestClass<FilePilot::Test::FileTableModelTest>(
        "FileTableModelTest", argc, argv);
    status |= runTestClass<FilePilot::Test::FileOrganizePageTest>(
        "FileOrganizePageTest", argc, argv);
    status |= runTestClass<FilePilot::Test::SettingsServiceTest>(
        "SettingsServiceTest", argc, argv);
    status |= runTestClass<FilePilot::Test::LogManagerTest>(
        "LogManagerTest", argc, argv);
    status |= runTestClass<FilePilot::Test::MainWindowTest>(
        "MainWindowTest", argc, argv);
    status |= runTestClass<FilePilot::Test::ThemePaletteTest>(
        "ThemePaletteTest", argc, argv);
    status |= runTestClass<FilePilot::Test::ThemeDetectorTest>(
        "ThemeDetectorTest", argc, argv);
    status |= runTestClass<FilePilot::Test::MainWindowThemeSmokeTest>(
        "MainWindowThemeSmokeTest", argc, argv);
    status |= runTestClass<FilePilot::Test::DpiLayoutTest>(
        "DpiLayoutTest", argc, argv);

    return status;
}
