#include "Theme.h"
#include "Window.h"

#include <QApplication>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    ConfigureLightTheme(app);
    Window window;
    window.show();
    return QApplication::exec();
}
