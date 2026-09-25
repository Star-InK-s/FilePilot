#pragma once

#include <QWidget>

class QString;

namespace FilePilot {

class PlaceholderPage : public QWidget
{
public:
    explicit PlaceholderPage(const QString &title, QWidget *parent = nullptr);
};

class DuplicateFilesPage : public PlaceholderPage
{
public:
    explicit DuplicateFilesPage(QWidget *parent = nullptr);
};

class BackupPage : public PlaceholderPage
{
public:
    explicit BackupPage(QWidget *parent = nullptr);
};

class HistoryPage : public PlaceholderPage
{
public:
    explicit HistoryPage(QWidget *parent = nullptr);
};

class SettingsPage : public PlaceholderPage
{
public:
    explicit SettingsPage(QWidget *parent = nullptr);
};

} // namespace FilePilot
