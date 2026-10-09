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

class Window : public QMainWindow {
public:
  Window();

protected:
  void changeEvent(QEvent *event) override;
  void closeEvent(QCloseEvent *event) override;

private:
  void openVault();
  void selectNote(const QString &path);
  void saveNote();
  void refresh();
  void replaceEditor(const QString &text);
  void watchNote();
  void syncWithDisk();
  void showConflictBar(DiskChange change);
  void reloadFromDisk();
  void keepMine();
  void discardEdits();

  Session session_;
  QFileSystemWatcher *watcher_;
  QTimer *syncTimer_;
  QLineEdit *folder_;
  QListWidget *notes_;
  QPlainTextEdit *editor_;
  QPushButton *save_;
  QWidget *conflictBar_;
  QLabel *conflictLabel_;
  QPushButton *reload_;
  QPushButton *keepMine_;
  QPushButton *discard_;
  QLabel *filename_;
  QLabel *status_;
};
