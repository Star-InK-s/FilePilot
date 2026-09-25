#include "app/MainWindow.h"

#include <QLabel>

namespace FilePilot {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("FilePilot"));
    resize(1100, 720);

    auto *placeholder = new QLabel(QStringLiteral("FilePilot"), this);
    placeholder->setAlignment(Qt::AlignCenter);
    setCentralWidget(placeholder);
}

} // namespace FilePilot
