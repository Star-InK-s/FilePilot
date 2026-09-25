#include "app/Application.h"
#include "app/MainWindow.h"

int main(int argc, char *argv[])
{
    FilePilot::Application application(argc, argv);
    FilePilot::MainWindow window(application);
    window.show();

    return application.exec();
}
