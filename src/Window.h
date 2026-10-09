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

  Session session_;
  QFileSystemWatcher *watcher_;
  QTimer *syncTimer_;
  QLineEdit *folder_;
  QListWidget *notes_;
  QPlainTextEdit *editor_;
  QPushButton *save_;
  QLabel *filename_;
  QLabel *status_;
};
