#pragma once

#include "Session.h"
#include "SettingsStore.h"

#include <QMainWindow>
#include <QUrl>

#include <functional>
#include <memory>
#include <optional>

class NoteTree;
class QAction;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTimer;
class QCloseEvent;
class QFileSystemWatcher;

// Asks for a vault folder; returns nothing when the user cancels.
using FolderPicker = std::function<std::optional<QString>(QWidget *parent)>;

class Window : public QMainWindow
{
public:
    explicit Window(
        TrashFunction trash = moveToSystemTrash,
        std::function<bool(const QUrl &)> openAttachment = {},
        FolderPicker pickFolder = {},
        std::shared_ptr<SettingsStore> settings = {});

protected:
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void openVault();
    bool openVaultFolder(const QString &folder, QString *error);
    void reopenLastVault();
    void selectNote(const QString &path);
    // Selects a note, then reveals it and focuses the editor if it opened.
    void openNote(const QString &path);
    void saveNote();
    void followLink();
    QWidget *createEmptyState();
    void refresh();
    void replaceEditor(const QString &text);
    void watchNote();
    void updateWatches();
    void syncWithDisk();
    void showConflictBar(DiskChange change);
    void reloadFromDisk();
    void keepMine();
    void mergeOutsideChanges();
    void saveAsNewNote();
    void discardEdits();
    void showNotes();
    void createNote();
    void trashNote();
    void moveNote();

    Session mSession;
    std::function<bool(const QUrl &)> mOpenAttachment;
    FolderPicker mPickFolder;
    std::shared_ptr<SettingsStore> mSettings;
    QFileSystemWatcher *mWatcher;
    QTimer *mSyncTimer;
    NoteTree *mNotes;
    QPlainTextEdit *mEditor;
    QPushButton *mSave;
    QPushButton *mFollow;
    QPushButton *mDelete;
    QPushButton *mMove;
    QLineEdit *mNewPath;
    QPushButton *mCreate;
    QWidget *mDeleteBar;
    QLabel *mDeleteLabel;
    QString mDeletePath; // The note the delete confirmation was shown for.
    QWidget *mConflictBar;
    QLabel *mConflictLabel;
    QPushButton *mReload;
    QPushButton *mKeepMine;
    QPushButton *mMerge;
    QPushButton *mSaveAsNew;
    QPushButton *mDiscard;
    QAction *mOpenVault;
    QStackedWidget *mPages;
    QLabel *mEmptyMessage;
    QWidget *mEmptyState; // Built from mOpenVault and mEmptyMessage, so declared after them.
    QWidget *mVaultView = nullptr;
    QLabel *mFilename;
    QLabel *mStatus;
};
