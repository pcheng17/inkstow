#include "Vault.h"

#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);
    if (app.arguments().size() != 2) {
        err << "Usage: vault-inspect <vault-folder>\n";
        return 1;
    }
    QString error;
    const auto vault = Vault::open(app.arguments().at(1), &error);
    QStringList notes;
    if (!vault || !vault->listNotes(&notes, &error)) {
        err << "Vault error: " << error << '\n';
        return 1;
    }
    for (const QString &note : notes)
        out << note << '\n';
    return 0;
}
