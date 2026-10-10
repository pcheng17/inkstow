#pragma once

#include "Session.h"

#include <QMainWindow>

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
    explicit Window(TrashFunction trash = MoveToSystemTrash);

protected:
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void OpenVault();
    void SelectNote(const QString &path);
    void SaveNote();
    void Refresh();
    void ReplaceEditor(const QString &text);
    void WatchNote();
    void UpdateWatches();
    void SyncWithDisk();
    void ShowConflictBar(DiskChange change);
    void ReloadFromDisk();
    void KeepMine();
    void MergeOutsideChanges();
    void SaveAsNewNote();
    void DiscardEdits();
    void ShowNotes();
    void CreateNote();
    void TrashNote();

    Session mSession;
    QFileSystemWatcher *mWatcher;
    QTimer *mSyncTimer;
    QLineEdit *mFolder;
    QListWidget *mNotes;
    QPlainTextEdit *mEditor;
    QPushButton *mSave;
    QPushButton *mDelete;
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
