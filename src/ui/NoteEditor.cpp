#include "NoteEditor.h"

#include "MarkdownHighlighter.h"
#include "Theme.h"

#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>

#include <algorithm>
#include <utility>

namespace
{
constexpr int kParagraphSeparatorProperty = QTextFormat::UserProperty + 1;

void tagBlock(const QTextBlock& block, bool paragraphSeparator)
{
    QTextBlockFormat format;
    format.setProperty(kParagraphSeparatorProperty, paragraphSeparator);
    QTextCursor cursor(block);
    cursor.mergeBlockFormat(format);
}
} // namespace

NoteEditor::NoteEditor(std::function<void()> followLink)
    : mFollowLink(std::move(followLink))
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

void NoteEditor::setNoteText(const QString& text)
{
    setPlainText(text);
    document()->setUndoRedoEnabled(false);
    tagParagraphSeparators(text, 0);
    // New paragraphs inherit the spacing from the one they are split from.
    QTextBlockFormat spacing;
    spacing.setLineHeight(theme::kEditorLineHeightPercent, QTextBlockFormat::ProportionalHeight);
    QTextCursor all(document());
    all.select(QTextCursor::Document);
    all.mergeBlockFormat(spacing);
    document()->setUndoRedoEnabled(true);
}

void NoteEditor::replaceNoteTextUndoable(const QString& text)
{
    QTextCursor cursor(document());
    cursor.select(QTextCursor::Document);
    cursor.beginEditBlock();
    cursor.insertText(text);
    tagParagraphSeparators(text, 0);
    cursor.endEditBlock();
}

void NoteEditor::tagParagraphSeparators(const QString& text, int start)
{
    // Qt converts U+2029, CR and LF to identical block boundaries. Store the
    // original kind in the next block's undoable format, not transient userData.
    int position = start;
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text[i] == u'\r' && i + 1 < text.size() && text[i + 1] == u'\n') ++i;
        ++position;
        if (text[i] == u'\r' || text[i] == u'\n' || text[i] == u'\u2029')
            tagBlock(document()->findBlock(position), text[i] == u'\u2029');
    }
}

QString NoteEditor::rawNoteText() const
{
    QString text = document()->toRawText();
    for (QTextBlock block = document()->firstBlock(); block.next().isValid(); block = block.next())
        text[block.position() + block.length() - 1] =
            block.next().blockFormat().boolProperty(kParagraphSeparatorProperty) ? u'\u2029'
                                                                                 : u'\n';
    return text;
}

void NoteEditor::insertNoteText(const QString& text)
{
    QTextCursor cursor = textCursor();
    if (!text.contains(u'\u2029')) {
        cursor.insertText(text);
        return;
    }
    const int start = cursor.selectionStart();
    cursor.beginEditBlock();
    cursor.insertText(text);
    tagParagraphSeparators(text, start);
    cursor.endEditBlock();
    // insertText emits textChanged before the separator tags can be applied.
    emit textChanged();
}

void NoteEditor::insertFromMimeData(const QMimeData* source)
{
    // Inserted like typing so new paragraphs keep the line spacing; a pasted
    // fragment would bring default paragraph formats with it.
    if (!source->hasText()) return;
    insertNoteText(source->text());
    ensureCursorVisible();
}

void NoteEditor::keyPressEvent(QKeyEvent* event)
{
    if (event->text().contains(u'\u2029') && !event->modifiers().testFlag(Qt::ControlModifier)) {
        insertNoteText(event->text());
        event->accept();
        return;
    }
    QTextEdit::keyPressEvent(event);
}

void NoteEditor::mouseReleaseEvent(QMouseEvent* event)
{
    QTextEdit::mouseReleaseEvent(event);
    // Qt reports Cmd as the control modifier on macOS. A drag selects instead.
    if (event->button() != Qt::LeftButton || !event->modifiers().testFlag(Qt::ControlModifier) ||
        textCursor().hasSelection())
        return;
    setTextCursor(cursorForPosition(event->position().toPoint()));
    mFollowLink();
}

void NoteEditor::resizeEvent(QResizeEvent* event)
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
    const QScrollBar* scrollBar = verticalScrollBar();
    const int scrollBarWidth = scrollBar->maximum() > 0 ? scrollBar->sizeHint().width() : 0;
    setViewportMargins(side, 0, std::max(0, side - scrollBarWidth), 0);
}
