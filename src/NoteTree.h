#pragma once

#include <QTreeWidget>

#include <functional>

// The vault's folders and notes as a tree. Folders come before notes, each
// sorted by name, and notes are shown without ".md". Every item keeps its
// vault-relative path. Arrow keys only move the cursor; Enter or a click asks
// for a note to be opened.
class NoteTree : public QTreeWidget
{
public:
    static constexpr int kFolderItem = QTreeWidgetItem::UserType;
    static constexpr int kNoteItem = QTreeWidgetItem::UserType + 1;
    static constexpr int kPathRole = Qt::UserRole;

    explicit NoteTree(std::function<void(const QString &path)> openNote);

    // Rebuilds from vault-relative paths, keeping expanded folders and the current item.
    void setNotes(const QStringList &notes, const QStringList &folders);
    // Makes the note current and expands its folders; clears the current item
    // when the note is not listed.
    void revealNote(const QString &path);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    QTreeWidgetItem *findItem(int type, const QString &path);
    void openItem(const QTreeWidgetItem *item);

    std::function<void(const QString &path)> mOpenNote;
};
