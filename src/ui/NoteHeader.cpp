#include "NoteHeader.h"

#include "Theme.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>

NoteHeader::NoteHeader()
    : mTitle(new QLabel), mUnsaved(new QLabel(QStringLiteral("●")))
{
    setObjectName(QStringLiteral("noteHeader"));
    mTitle->setObjectName(QStringLiteral("noteTitle"));
    mTitle->setFont(theme::titleFont(mTitle->font()));
    mUnsaved->setObjectName(QStringLiteral("unsavedIndicator"));
    mUnsaved->setAccessibleName(QStringLiteral("Unsaved changes"));
    mUnsaved->setToolTip(QStringLiteral("Unsaved changes"));
    mUnsaved->setFont(theme::titleFont(mUnsaved->font()));
    QPalette palette = mUnsaved->palette();
    palette.setColor(QPalette::WindowText, QColor(theme::kUnsavedInk));
    mUnsaved->setPalette(palette);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(theme::kFlushMargins);
    layout->setSpacing(theme::kHeaderSpacing);
    layout->addWidget(mTitle);
    layout->addWidget(mUnsaved);
    layout->addStretch(1);
    showNote(QString(), false);
}

void NoteHeader::showNote(const QString &path, bool unsaved)
{
    setHidden(path.isEmpty());
    mTitle->setText(QFileInfo(path).completeBaseName());
    mTitle->setToolTip(path);
    mUnsaved->setHidden(!unsaved);
}
