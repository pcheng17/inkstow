#pragma once

#include <QWidget>

class QLabel;

// Names the open note above the editor, without ".md" and with its
// vault-relative path as a tooltip, and shows whether it has unsaved edits.
// Hidden when no note is open.
class NoteHeader : public QWidget
{
public:
    NoteHeader();

    // path is vault-relative, or empty when no note is open.
    void showNote(const QString& path, bool unsaved);

private:
    QLabel* mTitle;
    QLabel* mUnsaved;
};
