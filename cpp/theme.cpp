#include "theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

void configureLightTheme(QApplication &app) {
  app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
  QPalette light;
  light.setColor(QPalette::Window, QColor(QStringLiteral("#faf9fc")));
  light.setColor(QPalette::WindowText, QColor(QStringLiteral("#24212b")));
  light.setColor(QPalette::Base, Qt::white);
  light.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#faf9fc")));
  light.setColor(QPalette::Text, QColor(QStringLiteral("#24212b")));
  light.setColor(QPalette::Button, Qt::white);
  light.setColor(QPalette::ButtonText, QColor(QStringLiteral("#24212b")));
  light.setColor(QPalette::Highlight, QColor(QStringLiteral("#e7def4")));
  light.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#24212b")));
  app.setPalette(light);
}
