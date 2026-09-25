#pragma once

#include <QApplication>

namespace FilePilot {

class Application : public QApplication
{
public:
    Application(int &argc, char **argv);
};

} // namespace FilePilot
