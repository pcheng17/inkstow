#include "NoteEditor.h"

#include "Theme.h"

#include <QMimeData>
#include <QTextBlockFormat>
#include <QTextCursor>

NoteEditor::NoteEditor()
{
    setAcceptRichText(false);
    setFont(theme::editorFont(font()));
}

void NoteEditor::setNoteText(const QString &text)
{
    setPlainText(text);
    // New paragraphs inherit the spacing from the one they are split from.
    QTextBlockFormat spacing;
    spacing.setLineHeight(theme::kEditorLineHeightPercent, QTextBlockFormat::ProportionalHeight);
    QTextCursor all(document());
    all.select(QTextCursor::Document);
    document()->setUndoRedoEnabled(false);
    all.mergeBlockFormat(spacing);
    document()->setUndoRedoEnabled(true);
}

void NoteEditor::insertFromMimeData(const QMimeData *source)
{
    // Inserted like typing so new paragraphs keep the line spacing; a pasted
    // fragment would bring default paragraph formats with it.
    if (!source->hasText()) return;
    textCursor().insertText(source->text());
    ensureCursorVisible();
}
