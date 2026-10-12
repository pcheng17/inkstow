#pragma once

#include <QTextEdit>

// Edits a note's raw Markdown as plain text, in the theme's font and line
// spacing, with padding around the text and its width capped and centred.
// Markdown is highlighted as it is typed, without changing the text.
// Pasted or dropped rich text is inserted as plain text, so the
// document always holds exactly what is saved.
class NoteEditor : public QTextEdit
{
public:
    NoteEditor();

    // Replaces the text, clearing undo history.
    void setNoteText(const QString &text);

protected:
    void insertFromMimeData(const QMimeData *source) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void centreText();
};
