#pragma once

#include "Theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPushButton>

#include <initializer_list>

// One message-and-actions layout for conflict, missing-note and delete prompts.
class PromptBanner : public QFrame
{
public:
    explicit PromptBanner(std::initializer_list<QPushButton*> actions)
        : mMessage(new QLabel(this))
    {
        setFrameShape(QFrame::StyledPanel);
        setAutoFillBackground(true);
        QPalette colors = palette();
        colors.setColor(QPalette::Window, QColor(theme::kPromptBackground));
        colors.setColor(QPalette::WindowText, QColor(theme::kInk));
        setPalette(colors);
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(theme::kPromptMargins);
        layout->setSpacing(theme::kPromptSpacing);
        mMessage->setWordWrap(true);
        layout->addWidget(mMessage, 1);
        for (QPushButton* action : actions) {
            action->setAccessibleName(action->text());
            layout->addWidget(action);
        }
    }

    void setMessage(const QString& message)
    {
        mMessage->setText(message);
        setAccessibleName(message);
    }

private:
    QLabel* mMessage;
};
