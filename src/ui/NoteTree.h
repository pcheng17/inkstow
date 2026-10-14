#pragma once

#include <QTreeWidget>

#include <functional>
#include <memory>

class QAction;
class QMenu;

// The vault's folders and notes as a tree. Folders come before notes, each
// sorted by name, and notes are shown without ".md". Every item keeps its
// vault-relative path. Arrow keys only move the cursor; Enter or a click asks
// for a note to be opened. Right-clicking a note asks for it to be opened, then
// offers the note actions.
class NoteTree : public QTreeWidget
{
public:
    static constexpr int kFolderItem = QTreeWidgetItem::UserType;
    static constexpr int kNoteItem = QTreeWidgetItem::UserType + 1;
    static constexpr int kPathRole = Qt::UserRole;

    explicit NoteTree(std::function<void(const QString& path)> openNote);

    // Rebuilds from vault-relative paths, keeping expanded folders and the current item.
    void setNotes(const QStringList& notes, const QStringList& folders);
    // Makes the note current and expands its folders; clears the current item
    // when the note is not listed.
    void revealNote(const QString& path);
    // The actions the context menu offers for the open note.
    void setNoteActions(const QList<QAction*>& actions);
    // Asks for the note at point (in viewport coordinates) to be opened and
    // returns a menu of the note actions, or null when point is not on a note
    // or the note did not open.
    std::unique_ptr<QMenu> contextMenuAt(const QPoint& point);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QTreeWidgetItem* findItem(int type, const QString& path);
    void openItem(const QTreeWidgetItem* item);

    std::function<void(const QString& path)> mOpenNote;
    QList<QAction*> mNoteActions;
};
