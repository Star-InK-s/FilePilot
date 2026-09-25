#include "ui/pages/Pages.h"

#include <QLabel>
#include <QVBoxLayout>

namespace FilePilot {

PlaceholderPage::PlaceholderPage(const QString &title, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 28, 32, 28);
    layout->setSpacing(18);

    auto *titleLabel = new QLabel(title, this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(16);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    layout->addWidget(titleLabel);

    auto *emptyLabel = new QLabel(QStringLiteral("暂无数据"), this);
    emptyLabel->setObjectName(QStringLiteral("emptyStateLabel"));
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLabel->setEnabled(false);
    layout->addWidget(emptyLabel, 1);
}

DuplicateFilesPage::DuplicateFilesPage(QWidget *parent)
    : PlaceholderPage(QStringLiteral("重复文件"), parent)
{
    setObjectName(QStringLiteral("pageDuplicates"));
}

BackupPage::BackupPage(QWidget *parent)
    : PlaceholderPage(QStringLiteral("备份"), parent)
{
    setObjectName(QStringLiteral("pageBackup"));
}

HistoryPage::HistoryPage(QWidget *parent)
    : PlaceholderPage(QStringLiteral("历史记录"), parent)
{
    setObjectName(QStringLiteral("pageHistory"));
}

SettingsPage::SettingsPage(QWidget *parent)
    : PlaceholderPage(QStringLiteral("设置"), parent)
{
    setObjectName(QStringLiteral("pageSettings"));
}

} // namespace FilePilot
