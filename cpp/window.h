#pragma once

#include "session.h"

#include <QMainWindow>

class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QCloseEvent;

class Window : public QMainWindow {
public:
  Window();

protected:
  void closeEvent(QCloseEvent *event) override;

private:
  void openVault();
  void selectNote(const QString &path);
  void saveNote();
  void refresh();
  void replaceEditor(const QString &text);

  Session session_;
  QLineEdit *folder_;
  QListWidget *notes_;
  QPlainTextEdit *editor_;
  QPushButton *save_;
  QLabel *filename_;
  QLabel *status_;
};
