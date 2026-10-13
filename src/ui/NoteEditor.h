#pragma once

#include <QTextEdit>

#include <functional>

// Edits a note's raw Markdown as plain text, in the theme's font and line
// spacing, with padding around the text and its width capped and centred.
// Markdown is highlighted as it is typed, without changing the text.
// Pasted or dropped rich text is inserted as plain text, so the
// document always holds exactly what is saved. Ctrl+click (Cmd+click on
// macOS) moves the cursor to the click and asks for the link there to be
// followed.
class NoteEditor : public QTextEdit
{
public:
    explicit NoteEditor(std::function<void()> followLink);

    // Replaces the text, clearing undo history.
    void setNoteText(const QString &text);
    // QTextDocument uses U+2029 for every paragraph break, including LF.
    // Restore LF except for separators tagged as literal U+2029.
    QString noteText() const;
    void rememberParagraphSeparators(const QString &text);

protected:
    void insertFromMimeData(const QMimeData *source) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void centreText();
    void insertNoteText(const QString &inserted);

    std::function<void()> mFollowLink;
};
