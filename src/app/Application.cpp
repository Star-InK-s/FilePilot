#include "app/Application.h"

namespace FilePilot {

Application::Application(int &argc, char **argv)
    : QApplication(argc, argv)
{
    setApplicationName(QStringLiteral("FilePilot"));
    setApplicationDisplayName(QStringLiteral("FilePilot"));
    setApplicationVersion(QStringLiteral("0.1.0"));
    setOrganizationName(QStringLiteral("FilePilot"));
}

} // namespace FilePilot
