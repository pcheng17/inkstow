#include "NoteEditor.h"

#include "MarkdownHighlighter.h"
#include "Theme.h"

#include <QMimeData>
#include <QScrollBar>
#include <QTextBlockFormat>
#include <QTextCursor>

#include <algorithm>

NoteEditor::NoteEditor()
{
    setAcceptRichText(false);
    setFont(theme::editorFont(font()));
    document()->setDocumentMargin(theme::kEditorPadding);
    new MarkdownHighlighter(document()); // Owned by the document, which setNoteText keeps.
    // Side margins share the text's background, so a wide editor reads as one page.
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    connect(verticalScrollBar(), &QScrollBar::rangeChanged, this, [this] { centreText(); });
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
    centreText();
    QTextEdit::resizeEvent(event);
}

void NoteEditor::centreText()
{
    // The document margin pads each side; side margins take up any width
    // beyond the cap so the text stays centred in the editor. A scroll bar
    // takes its width from the right margin rather than from the text.
    const int spare =
        contentsRect().width() - theme::kEditorMaxTextWidth - (2 * theme::kEditorPadding);
    const int side = std::max(0, spare / 2);
    const QScrollBar *scrollBar = verticalScrollBar();
    const int scrollBarWidth = scrollBar->maximum() > 0 ? scrollBar->sizeHint().width() : 0;
    setViewportMargins(side, 0, std::max(0, side - scrollBarWidth), 0);
}
