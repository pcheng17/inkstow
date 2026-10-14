#pragma once

#include "NoteTree.h"
#include "SettingsStore.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QFile>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QThread>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include <stdexcept>
#include <string>

#define CHECK(value)                                                                               \
    do {                                                                                           \
        if (!(value))                                                                              \
            throw std::runtime_error("Failed: " #value " at line " + std::to_string(__LINE__));    \
    } while (false)

inline void writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    CHECK(file.write(bytes) == bytes.size());
}

inline QByteArray readFile(const QString& path)
{
    QFile file(path);
    CHECK(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

// Lets the file watcher deliver its notifications.
template<typename Done>
bool waitFor(Done done)
{
    QDeadlineTimer deadline(5000);
    while (!done() && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(10);
    }
    return done();
}

// Remembers settings for one test instead of touching the user's real settings.
class MemorySettings : public SettingsStore
{
public:
    QString lastVault() const override { return mLastVault; }
    bool setLastVault(const QString& path) override
    {
        mLastVault = path;
        return true;
    }

private:
    QString mLastVault;
};

inline bool refuseTrash(const QString& /*path*/, QString* /*error*/)
{
    return false;
}

// Finds the tree item holding a vault-relative note or folder path.
inline QTreeWidgetItem* treeItem(QTreeWidget* tree, const QString& path)
{
    for (QTreeWidgetItemIterator it(tree); *it; ++it) {
        if ((*it)->data(0, NoteTree::kPathRole).toString() == path) return *it;
    }
    return nullptr;
}

inline QString currentPath(const QTreeWidget* tree)
{
    const QTreeWidgetItem* item = tree->currentItem();
    return item ? item->data(0, NoteTree::kPathRole).toString() : QString();
}

inline QStringList notePaths(QTreeWidget* tree)
{
    QStringList paths;
    for (QTreeWidgetItemIterator it(tree); *it; ++it) {
        if ((*it)->type() == NoteTree::kNoteItem)
            paths.append((*it)->data(0, NoteTree::kPathRole).toString());
    }
    return paths;
}

inline void pressKey(QWidget* widget, int key)
{
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &press);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &release);
}

// Presses and releases the left button at point in a widget, with modifiers held.
inline void clickAt(QWidget* widget, QPointF point, Qt::KeyboardModifiers modifiers = {})
{
    const QPointF global = widget->mapToGlobal(point);
    QMouseEvent
        press(QEvent::MouseButtonPress, point, global, Qt::LeftButton, Qt::LeftButton, modifiers);
    QCoreApplication::sendEvent(widget, &press);
    QMouseEvent
        release(QEvent::MouseButtonRelease, point, global, Qt::LeftButton, Qt::NoButton, modifiers);
    QCoreApplication::sendEvent(widget, &release);
}

inline void clickItem(QTreeWidget* tree, QTreeWidgetItem* item)
{
    CHECK(item);
    tree->scrollToItem(item);
    clickAt(tree->viewport(), tree->visualItemRect(item).center());
}

// Moves to a note with the keyboard and presses Enter, as a user would.
inline void openNote(QTreeWidget* tree, const QString& path)
{
    QTreeWidgetItem* item = treeItem(tree, path);
    CHECK(item);
    tree->setCurrentItem(item);
    pressKey(tree, Qt::Key_Return);
}

// The note actions tests, in NoteActionTests.cpp.
void runNoteActionTests();
