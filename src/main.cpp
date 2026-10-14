#include "Theme.h"
#include "Window.h"

#include <QApplication>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    // Names Qt's per-user settings, which remember the last vault.
    QApplication::setOrganizationName(QStringLiteral("Inkstow"));
    QApplication::setApplicationName(QStringLiteral("Inkstow"));
    configureLightTheme(app);
    Window window;
    window.show();
    return QApplication::exec();
}
