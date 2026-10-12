#include "NoteTree.h"

#include <QHash>
#include <QKeyEvent>
#include <QSet>
#include <QTreeWidgetItemIterator>

#include <algorithm>

namespace
{
constexpr auto kNoteSuffix = QLatin1StringView(".md");

using FolderItems = QHash<QString, QTreeWidgetItem *>;

// Returns the item for a vault-relative folder, creating it and its parents.
QTreeWidgetItem *addFolder(FolderItems &folders, const QString &path)
{
    if (QTreeWidgetItem *existing = folders.value(path)) return existing;
    QTreeWidgetItem *parent = addFolder(folders, path.section(u'/', 0, -2));
    auto *item = new QTreeWidgetItem(parent, {path.section(u'/', -1)}, NoteTree::kFolderItem);
    item->setData(0, NoteTree::kPathRole, path);
    folders.insert(path, item);
    return item;
}

void addNote(FolderItems &folders, const QString &path)
{
    QTreeWidgetItem *parent = addFolder(folders, path.section(u'/', 0, -2));
    const QString name = path.section(u'/', -1).chopped(kNoteSuffix.size());
    auto *item = new QTreeWidgetItem(parent, {name}, NoteTree::kNoteItem);
    item->setData(0, NoteTree::kPathRole, path);
}

// Folders before notes, then by name ignoring case, with case as the tiebreak.
bool comesBefore(const QTreeWidgetItem *a, const QTreeWidgetItem *b)
{
    if (a->type() != b->type()) return a->type() == NoteTree::kFolderItem;
    const int folded = QString::compare(a->text(0), b->text(0), Qt::CaseInsensitive);
    return folded != 0 ? folded < 0 : a->text(0) < b->text(0);
}

void sortChildren(QTreeWidgetItem *parent)
{
    QList<QTreeWidgetItem *> children = parent->takeChildren();
    std::stable_sort(children.begin(), children.end(), comesBefore);
    parent->addChildren(children);
    for (QTreeWidgetItem *child : children)
        sortChildren(child);
}

QString itemPath(const QTreeWidgetItem *item)
{
    return item->data(0, NoteTree::kPathRole).toString();
}
} // namespace

NoteTree::NoteTree(std::function<void(const QString &path)> openNote)
    : mOpenNote(std::move(openNote))
{
    setHeaderHidden(true);
    connect(this, &QTreeWidget::itemClicked, this, [this](const QTreeWidgetItem *item) {
        openItem(item);
    });
}

void NoteTree::setNotes(const QStringList &notes, const QStringList &folders)
{
    QSet<QString> expanded;
    for (QTreeWidgetItemIterator it(this); *it; ++it) {
        if ((*it)->isExpanded()) expanded.insert(itemPath(*it));
    }
    const QTreeWidgetItem *current = currentItem();
    const int currentType = current ? current->type() : kNoteItem;
    const QString currentPath = current ? itemPath(current) : QString();

    QTreeWidgetItem root;
    FolderItems folderItems{
        {QString(), &root}
    };
    for (const QString &folder : folders)
        addFolder(folderItems, folder);
    for (const QString &note : notes)
        addNote(folderItems, note);
    sortChildren(&root);
    clear();
    addTopLevelItems(root.takeChildren());
    for (QTreeWidgetItemIterator it(this); *it; ++it) {
        if (expanded.contains(itemPath(*it))) (*it)->setExpanded(true);
    }
    setCurrentItem(findItem(currentType, currentPath));
}

void NoteTree::revealNote(const QString &path)
{
    QTreeWidgetItem *item = findItem(kNoteItem, path);
    for (QTreeWidgetItem *folder = item ? item->parent() : nullptr; folder;
         folder = folder->parent())
        folder->setExpanded(true);
    setCurrentItem(item);
    if (item) scrollToItem(item);
}

void NoteTree::keyPressEvent(QKeyEvent *event)
{
    QTreeWidgetItem *item = currentItem();
    if (item && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        if (item->type() == kFolderItem)
            item->setExpanded(!item->isExpanded());
        else
            openItem(item);
        event->accept();
        return;
    }
    QTreeWidget::keyPressEvent(event);
}

QTreeWidgetItem *NoteTree::findItem(int type, const QString &path)
{
    if (path.isEmpty()) return nullptr;
    for (QTreeWidgetItemIterator it(this); *it; ++it) {
        if ((*it)->type() == type && itemPath(*it) == path) return *it;
    }
    return nullptr;
}

void NoteTree::openItem(const QTreeWidgetItem *item)
{
    if (item->type() == kNoteItem) mOpenNote(itemPath(item));
}
