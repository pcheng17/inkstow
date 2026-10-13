#pragma once

#include "Theme.h"

#include <QLabel>
#include <QPalette>

// Keeps the existing status label interface while distinguishing errors visually.
class StatusLabel : public QLabel
{
public:
    void setText(const QString &message)
    {
        QLabel::setText(message);
        const bool error = message.startsWith(QStringLiteral("Error:"));
        QPalette colors = palette();
        colors.setColor(QPalette::WindowText, QColor(error ? theme::kErrorInk : theme::kInk));
        colors.setColor(QPalette::Window, QColor(error ? theme::kErrorBackground : theme::kPaper));
        setPalette(colors);
        setAutoFillBackground(error);
    }
};
