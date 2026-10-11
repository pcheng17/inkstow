#pragma once

#include "Session.h"

#include <QMainWindow>
#include <QUrl>

#include <functional>

class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTimer;
class QCloseEvent;
class QFileSystemWatcher;

class Window : public QMainWindow
{
public:
    explicit Window(
        TrashFunction trash = moveToSystemTrash,
        std::function<bool(const QUrl &)> openAttachment = {});

protected:
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void openVault();
    void selectNote(const QString &path);
    void saveNote();
    void followLink();
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
    QFileSystemWatcher *mWatcher;
    QTimer *mSyncTimer;
    QLineEdit *mFolder;
    QListWidget *mNotes;
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
    QLabel *mFilename;
    QLabel *mStatus;
};
