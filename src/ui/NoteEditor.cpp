#include "NoteEditor.h"

#include "MarkdownHighlighter.h"
#include "Theme.h"

#include <QMimeData>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTextBlockFormat>
#include <QTextCursor>

#include <algorithm>
#include <utility>

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
    connect(
        document(),
        &QTextDocument::contentsChange,
        this,
        [this](int position, int removed, int added) {
            if (mParagraphSeparators.empty()) return;
            const QString raw = document()->toRawText();
            // Applying paragraph formatting also emits a whole-document
            // contentsChange, without changing any separator positions.
            if (raw == mPreviousRaw) return;
            mPreviousRaw = raw;
            for (auto &separator : mParagraphSeparators) {
                if (separator >= position && separator < position + removed)
                    separator = -1;
                else if (separator >= position)
                    separator += added - removed;
            }
            mParagraphSeparators.erase(
                std::remove(mParagraphSeparators.begin(), mParagraphSeparators.end(), -1),
                mParagraphSeparators.end());
        });
}

void NoteEditor::setNoteText(const QString &text)
{
    setPlainText(text);
    rememberParagraphSeparators(text);
    // New paragraphs inherit the spacing from the one they are split from.
    QTextBlockFormat spacing;
    spacing.setLineHeight(theme::kEditorLineHeightPercent, QTextBlockFormat::ProportionalHeight);
    QTextCursor all(document());
    all.select(QTextCursor::Document);
    document()->setUndoRedoEnabled(false);
    all.mergeBlockFormat(spacing);
    document()->setUndoRedoEnabled(true);
}

void NoteEditor::rememberParagraphSeparators(const QString &text)
{
    mParagraphSeparators.clear();
    for (qsizetype position = 0; position < text.size(); ++position)
        if (text[position] == QChar::ParagraphSeparator) mParagraphSeparators.push_back(position);
    mPreviousRaw = mParagraphSeparators.empty() ? QString() : document()->toRawText();
}

QString NoteEditor::noteText() const
{
    QString text = document()->toRawText();
    for (qsizetype position = 0; position < text.size(); ++position)
        if (text[position] == QChar::ParagraphSeparator &&
            !std::binary_search(mParagraphSeparators.begin(), mParagraphSeparators.end(), position))
            text[position] = u'\n';
    return text;
}

void NoteEditor::insertFromMimeData(const QMimeData *source)
{
    // Inserted like typing so new paragraphs keep the line spacing; a pasted
    // fragment would bring default paragraph formats with it.
    if (!source->hasText()) return;
    textCursor().insertText(source->text());
    ensureCursorVisible();
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
