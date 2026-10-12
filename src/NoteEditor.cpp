#include "NoteEditor.h"

#include "Theme.h"

#include <QMimeData>
#include <QTextBlockFormat>
#include <QTextCursor>

#include <algorithm>

NoteEditor::NoteEditor()
{
    setAcceptRichText(false);
    setFont(theme::editorFont(font()));
    document()->setDocumentMargin(theme::kEditorPadding);
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

void NoteEditor::resizeEvent(QResizeEvent *event)
{
    // The document margin pads each side; side margins take up any width
    // beyond the cap so the text stays centred.
    const int spare =
        contentsRect().width() - theme::kEditorMaxTextWidth - 2 * theme::kEditorPadding;
    const int side = std::max(0, spare / 2);
    setViewportMargins(side, 0, side, 0);
    QTextEdit::resizeEvent(event);
}
