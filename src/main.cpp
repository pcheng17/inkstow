#include "Theme.h"
#include "Window.h"

#include <QApplication>

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  configureLightTheme(app);
  Window window;
  window.show();
  return app.exec();
}
