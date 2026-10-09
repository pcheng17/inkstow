#include "theme.h"
#include "window.h"

#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>

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
    CHECK(folder && open && notes && editor && save && status);
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

    editor->setPlainText(QStringLiteral("local edit"));
    writeFile(note, "external edit");
    save->click();
    CHECK(status->text().contains(QStringLiteral("Save conflict")));
    CHECK(editor->toPlainText() == QStringLiteral("local edit"));
    CHECK(readFile(note) == "external edit");
    CHECK(save->isEnabled());
  } catch (const std::exception &error) {
    qCritical("%s", error.what());
    return 1;
  }
  return 0;
}
