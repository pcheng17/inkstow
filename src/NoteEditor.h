#pragma once

#include <QTextEdit>

// Edits a note's raw Markdown as plain text. Pasted or dropped rich text is
// inserted as plain text, so the document always holds exactly what is saved.
class NoteEditor : public QTextEdit
{
public:
    NoteEditor();
};
