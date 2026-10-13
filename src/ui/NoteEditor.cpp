#include "NoteEditor.h"

#include "MarkdownHighlighter.h"
#include "Theme.h"

#include <QInputMethodEvent>
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
// Block char formats belong to the separator immediately before the block.
// Unlike a parallel index list, they participate in QTextDocument undo/redo.
constexpr int kLiteralParagraphSeparator = QTextFormat::UserProperty + 1;

QString editorLineBreaks(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(u'\r', u'\n');
    return text;
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

void NoteEditor::setNoteText(const QString &text)
{
    setPlainText(text);
    document()->setUndoRedoEnabled(false);
    rememberParagraphSeparators(text);
    // New paragraphs inherit the spacing from the one they are split from.
    QTextBlockFormat spacing;
    spacing.setLineHeight(theme::kEditorLineHeightPercent, QTextBlockFormat::ProportionalHeight);
    QTextCursor all(document());
    all.select(QTextCursor::Document);
    all.mergeBlockFormat(spacing);
    document()->setUndoRedoEnabled(true);
}

void NoteEditor::rememberParagraphSeparators(const QString &text)
{
    for (QTextBlock block = document()->begin().next(); block.isValid(); block = block.next()) {
        const qsizetype position = block.position() - 1;
        const bool literal = text[position] == QChar::ParagraphSeparator;
        if (block.charFormat().property(kLiteralParagraphSeparator).toBool() == literal) continue;
        QTextCursor separator(document());
        separator.setPosition(static_cast<int>(position));
        separator.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        QTextCharFormat format;
        format.setProperty(kLiteralParagraphSeparator, literal);
        separator.mergeCharFormat(format);
    }
}

QString NoteEditor::noteText() const
{
    QString text = document()->toRawText();
    for (QTextBlock block = document()->begin().next(); block.isValid(); block = block.next()) {
        const qsizetype position = block.position() - 1;
        if (!block.charFormat().property(kLiteralParagraphSeparator).toBool())
            text[position] = u'\n';
    }
    return text;
}

void NoteEditor::insertNoteText(const QString &inserted)
{
    QTextCursor cursor = textCursor();
    if (inserted.contains(QChar::ParagraphSeparator)) {
        const QString before = noteText();
        const int start = cursor.selectionStart();
        const int end = cursor.selectionEnd();
        const QString normalized = editorLineBreaks(inserted);
        cursor.beginEditBlock();
        cursor.insertText(inserted);
        rememberParagraphSeparators(before.left(start) + normalized + before.mid(end));
        cursor.endEditBlock();
    } else {
        cursor.insertText(inserted);
    }
    setTextCursor(cursor);
    ensureCursorVisible();
}

void NoteEditor::insertFromMimeData(const QMimeData *source)
{
    // Inserted like typing so new paragraphs keep the line spacing; a pasted
    // fragment would bring default paragraph formats with it.
    if (source->hasText()) insertNoteText(source->text());
}

void NoteEditor::keyPressEvent(QKeyEvent *event)
{
    if (event->text().contains(QChar::ParagraphSeparator) &&
        !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)))
    {
        insertNoteText(event->text());
        event->accept();
        return;
    }
    QTextEdit::keyPressEvent(event);
}

void NoteEditor::inputMethodEvent(QInputMethodEvent *event)
{
    const QString committed = event->commitString();
    if (!committed.contains(QChar::ParagraphSeparator)) {
        QTextEdit::inputMethodEvent(event);
        return;
    }
    QTextCursor group = textCursor();
    group.beginEditBlock();
    QTextEdit::inputMethodEvent(event); // Let Qt handle composition and replacement ranges.
    const QString normalized = editorLineBreaks(committed);
    QString text = noteText();
    const qsizetype start = textCursor().position() - normalized.size();
    for (qsizetype index = 0; index < normalized.size(); ++index)
        if (normalized[index] == QChar::ParagraphSeparator && start + index >= 0 &&
            start + index < text.size())
            text[start + index] = QChar::ParagraphSeparator;
    rememberParagraphSeparators(text);
    group.endEditBlock();
}

void NoteEditor::mouseReleaseEvent(QMouseEvent *event)
{
    QTextEdit::mouseReleaseEvent(event);
    // Qt reports Cmd as the control modifier on macOS. A drag selects instead.
    if (event->button() != Qt::LeftButton || !event->modifiers().testFlag(Qt::ControlModifier) ||
        textCursor().hasSelection())
        return;
    setTextCursor(cursorForPosition(event->position().toPoint()));
    mFollowLink();
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
