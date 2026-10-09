#include "Theme.h"
#include "Window.h"

#include <QApplication>
#include <QDeadlineTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QThread>

#include <stdexcept>

#define CHECK(value)                                                           \
  do {                                                                         \
    if (!(value))                                                              \
      throw std::runtime_error("Failed: " #value " at line " +                 \
                               std::to_string(__LINE__));                      \
  } while (false)

static void writeFile(const QString &path, const QByteArray &bytes) {
  QFile file(path);
  CHECK(file.open(QIODevice::WriteOnly));
  CHECK(file.write(bytes) == bytes.size());
}

static QByteArray readFile(const QString &path) {
  QFile file(path);
  CHECK(file.open(QIODevice::ReadOnly));
  return file.readAll();
}

// Lets the file watcher deliver its notifications.
template <typename Done> static bool waitFor(Done done) {
  QDeadlineTimer deadline(5000);
  while (!done() && !deadline.hasExpired()) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(10);
  }
  return done();
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  try {
    configureLightTheme(app);
    CHECK(app.palette().color(QPalette::Window) ==
          QColor(QStringLiteral("#faf9fc")));
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString note = dir.filePath(QStringLiteral("note.md"));
    writeFile(note, "original");
    writeFile(dir.filePath(QStringLiteral("other.md")), "other");
    writeFile(dir.filePath(QStringLiteral(".hidden.md")), "hidden");

    Window window;
    auto *folder = window.findChild<QLineEdit *>(QStringLiteral("vaultFolder"));
    auto *open = window.findChild<QPushButton *>(QStringLiteral("openVault"));
    auto *notes = window.findChild<QListWidget *>(QStringLiteral("notes"));
    auto *editor = window.findChild<QPlainTextEdit *>(QStringLiteral("editor"));
    auto *save = window.findChild<QPushButton *>(QStringLiteral("saveNote"));
    auto *status = window.findChild<QLabel *>(QStringLiteral("status"));
    auto *conflictBar = window.findChild<QWidget *>(QStringLiteral("conflictBar"));
    auto *keepMine = window.findChild<QPushButton *>(QStringLiteral("keepMine"));
    auto *reload =
        window.findChild<QPushButton *>(QStringLiteral("reloadFromDisk"));
    auto *discard = window.findChild<QPushButton *>(QStringLiteral("discardEdits"));
    CHECK(folder && open && notes && editor && save && status && conflictBar &&
          keepMine && reload && discard);
    CHECK(conflictBar->isHidden());
    folder->setText(dir.path());
    open->click();
    CHECK(notes->count() == 2);
    CHECK(notes->item(0)->text() == QStringLiteral("note.md"));
    notes->setCurrentRow(0);
    CHECK(editor->toPlainText() == QStringLiteral("original"));
    editor->setPlainText(QStringLiteral("first edit"));
    CHECK(save->isEnabled());
    notes->setCurrentRow(1);
    CHECK(notes->currentRow() == 0); // Unsaved note cannot be switched away.
    save->click();
    CHECK(readFile(note) == "first edit");
    CHECK(!save->isEnabled());

    writeFile(note, "outside edit");
    CHECK(waitFor(
        [&] { return editor->toPlainText() == QStringLiteral("outside edit"); }));
    CHECK(!save->isEnabled());
    CHECK(status->text().contains(QStringLiteral("Reloaded")));

    // Atomic replacement, as many editors save, drops the file from the
    // watcher; later changes must still be noticed.
    for (const QByteArray bytes : {QByteArray("renamed once"),
                                   QByteArray("renamed twice")}) {
      QSaveFile replacement(note);
      CHECK(replacement.open(QIODevice::WriteOnly));
      CHECK(replacement.write(bytes) == bytes.size());
      CHECK(replacement.commit());
      CHECK(waitFor([&] {
        return editor->toPlainText() == QString::fromUtf8(bytes);
      }));
    }

    editor->setPlainText(QStringLiteral("local edit"));
    writeFile(note, "external edit");
    CHECK(waitFor(
        [&] { return status->text().contains(QStringLiteral("changed on disk")); }));
    CHECK(editor->toPlainText() == QStringLiteral("local edit"));
    save->click();
    CHECK(status->text().contains(QStringLiteral("Save conflict")));
    CHECK(editor->toPlainText() == QStringLiteral("local edit"));
    CHECK(readFile(note) == "external edit");
    CHECK(save->isEnabled());
    CHECK(!conflictBar->isHidden());

    keepMine->click();
    CHECK(readFile(note) == "local edit");
    CHECK(conflictBar->isHidden());
    CHECK(!save->isEnabled());

    editor->setPlainText(QStringLiteral("mine again"));
    writeFile(note, "theirs");
    CHECK(waitFor([&] { return !conflictBar->isHidden(); }));
    reload->click();
    CHECK(editor->toPlainText() == QStringLiteral("theirs"));
    CHECK(conflictBar->isHidden());
    CHECK(!save->isEnabled());
    editor->undo(); // Reloading can be undone, bringing the edits back.
    CHECK(editor->toPlainText() == QStringLiteral("mine again"));
    CHECK(save->isEnabled());
    CHECK(conflictBar->isHidden()); // The outside version has been seen.
    save->click();
    CHECK(readFile(note) == "mine again");

    // Save alone, before the watcher reports anything, also shows the bar.
    editor->setPlainText(QStringLiteral("mine 3"));
    writeFile(note, "theirs 3");
    save->click();
    CHECK(!conflictBar->isHidden());
    CHECK(!keepMine->isHidden() && discard->isHidden());
    reload->click();
    CHECK(editor->toPlainText() == QStringLiteral("theirs 3"));

    // A deleted note with unsaved edits can be discarded to move on.
    editor->setPlainText(QStringLiteral("orphaned"));
    CHECK(QFile::remove(note));
    CHECK(waitFor([&] { return !conflictBar->isHidden(); }));
    CHECK(!discard->isHidden() && keepMine->isHidden() && reload->isHidden());
    discard->click();
    CHECK(conflictBar->isHidden());
    CHECK(!save->isEnabled());
    notes->setCurrentRow(1);
    CHECK(notes->currentRow() == 1);
  } catch (const std::exception &error) {
    qCritical("%s", error.what());
    return 1;
  }
  return 0;
}
