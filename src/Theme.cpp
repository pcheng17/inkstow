#include "Theme.h"

#include <QApplication>
#include <QFont>
#include <QPalette>
#include <QStyleFactory>

QFont theme::titleFont(QFont base)
{
    base.setBold(true);
    base.setPointSize(kTitlePointSize);
    return base;
}

void configureLightTheme([[maybe_unused]] QApplication &app)
{
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette light;
    light.setColor(QPalette::Window, QColor(theme::kPaper));
    light.setColor(QPalette::WindowText, QColor(theme::kInk));
    light.setColor(QPalette::Base, QColor(theme::kSurface));
    light.setColor(QPalette::AlternateBase, QColor(theme::kPaper));
    light.setColor(QPalette::Text, QColor(theme::kInk));
    light.setColor(QPalette::Button, QColor(theme::kSurface));
    light.setColor(QPalette::ButtonText, QColor(theme::kInk));
    light.setColor(QPalette::Highlight, QColor(theme::kSelection));
    light.setColor(QPalette::HighlightedText, QColor(theme::kInk));
    QApplication::setPalette(light);
}
