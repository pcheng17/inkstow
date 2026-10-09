#include "Session.h"

#include <QDir>
#include <QFileInfo>
#include <QStringDecoder>

#include <algorithm>
#include <vector>

namespace
{
struct Change
{
    int start;
    int end;
    QStringList lines;
};

QStringList linesOf(const QString &text)
{
    return text.split(u'\n', Qt::KeepEmptyParts);
}

std::vector<Change> changes(const QStringList &base, const QStringList &other)
{
    const int n = base.size(), m = other.size();
    std::vector<std::vector<int>> lcs(n + 1, std::vector<int>(m + 1));
    for (int i = n - 1; i >= 0; --i)
        for (int j = m - 1; j >= 0; --j)
            lcs[i][j] = base[i] == other[j] ? 1 + lcs[i + 1][j + 1]
                                            : std::max(lcs[i + 1][j], lcs[i][j + 1]);
    std::vector<Change> result;
    int i = 0, j = 0;
    while (i < n || j < m) {
        if (i < n && j < m && base[i] == other[j]) {
            ++i;
            ++j;
            continue;
        }
        const int start = i;
        QStringList replacement;
        while (i < n || j < m) {
            if (i < n && j < m && base[i] == other[j]) break;
            if (j < m && (i == n || lcs[i][j + 1] >= lcs[i + 1][j]))
                replacement.append(other[j++]);
            else
                ++i;
        }
        result.push_back({start, i, replacement});
    }
    return result;
}

bool overlaps(const Change &a, const Change &b)
{
    if (a.start == a.end && b.start == b.end) return a.start == b.start;
    if (a.start == a.end) return b.start <= a.start && a.start < b.end;
    if (b.start == b.end) return a.start <= b.start && b.start < a.end;
    return a.start < b.end && b.start < a.end;
}

QStringList apply(const QStringList &base, int start, int end, const std::vector<Change> &hunks)
{
    QStringList result;
    int pos = start;
    for (const Change &hunk : hunks) {
        for (; pos < hunk.start; ++pos)
            result.append(base[pos]);
        result.append(hunk.lines);
        pos = hunk.end;
    }
    for (; pos < end; ++pos)
        result.append(base[pos]);
    return result;
}

QString threeWayMerge(const QString &baseText, const QString &mineText, const QString &outsideText)
{
    const QStringList base = linesOf(baseText);
    const auto mine = changes(base, linesOf(mineText));
    const auto outside = changes(base, linesOf(outsideText));
    QStringList result;
    size_t a = 0, b = 0;
    int pos = 0;
    while (a < mine.size() || b < outside.size()) {
        const int start = std::min(
            a < mine.size() ? mine[a].start : base.size(),
            b < outside.size() ? outside[b].start : base.size());
        for (; pos < start; ++pos)
            result.append(base[pos]);
        std::vector<Change> left, right;
        if (a < mine.size() && (b == outside.size() || mine[a].start <= outside[b].start))
            left.push_back(mine[a++]);
        else
            right.push_back(outside[b++]);
        bool expanded = true;
        while (expanded) {
            expanded = false;
            if (a < mine.size() && std::any_of(
                                       right.begin(),
                                       right.end(),
                                       [&](const Change &c) { return overlaps(mine[a], c); }))
            {
                left.push_back(mine[a++]);
                expanded = true;
            }
            if (b < outside.size() && std::any_of(
                                          left.begin(),
                                          left.end(),
                                          [&](const Change &c) { return overlaps(outside[b], c); }))
            {
                right.push_back(outside[b++]);
                expanded = true;
            }
        }
        int end = start;
        for (const Change &c : left)
            end = std::max(end, c.end);
        for (const Change &c : right)
            end = std::max(end, c.end);
        const QStringList l = apply(base, start, end, left);
        const QStringList r = apply(base, start, end, right);
        if (left.empty())
            result.append(r);
        else if (right.empty() || l == r)
            result.append(l);
        else {
            result.append(QStringLiteral("<<<<<<< mine"));
            result.append(l);
            result.append(QStringLiteral("======="));
            result.append(r);
            result.append(QStringLiteral(">>>>>>> outside"));
        }
        pos = end;
    }
    for (; pos < base.size(); ++pos)
        result.append(base[pos]);
    return result.join(u'\n');
}
} // namespace

bool Session::openVault(const QString &folder, QString *error)
{
    if (dirty()) {
        *error = QStringLiteral("Save the current note before opening another vault.");
        return false;
    }
    auto next = Vault::open(folder, error);
    if (!next) return false;
    QStringList notes;
    QStringList folders;
    if (!next->listNotes(&notes, error, &folders)) return false;
    vault_ = std::move(next);
    notes_ = notes;
    folders_ = folders;
    clearNote();
    return true;
}

bool Session::selectNote(const QString &path, QString *error)
{
    if (dirty()) {
        *error = QStringLiteral("Save the current note before switching.");
        return false;
    }
    if (!vault_ || !notes_.contains(path)) {
        *error = QStringLiteral("Note is not in the current vault.");
        return false;
    }
    QByteArray bytes;
    if (!vault_->readNote(path, &bytes, error)) return false;
    QString text;
    if (!decode(bytes, &text, error)) return false;
    load(path, bytes, text);
    return true;
}

bool Session::save(QString *error)
{
    return write(savedBytes_, error);
}

bool Session::write(const QByteArray &expected, QString *error)
{
    const Vault *vault = requireNote(error);
    if (!vault) return false;
    const QByteArray bytes =
        (hasBom_ ? QByteArray::fromHex("efbbbf") : QByteArray()) + content_.toUtf8();
    if (!vault->saveNote(path_, expected, bytes, error)) return false;
    savedContent_ = content_;
    savedBytes_ = bytes;
    conflictBytes_.reset();
    return true;
}

DiskChange Session::syncWithDisk(QString *error)
{
    if (!vault_ || path_.isEmpty()) return DiskChange::None;
    QByteArray bytes;
    if (!vault_->readNote(path_, &bytes, error)) return DiskChange::Missing;
    if (bytes == savedBytes_) return DiskChange::None;
    conflictBytes_ = bytes;
    if (dirty()) {
        *error = QStringLiteral(
            "Note changed on disk while you have unsaved edits; "
            "reload from disk or keep your version.");
        return DiskChange::Conflict;
    }
    QString text;
    if (!decode(bytes, &text, error)) return DiskChange::Conflict;
    load(path_, bytes, text);
    return DiskChange::Reloaded;
}

bool Session::reloadFromDisk(QString *error)
{
    const Vault *vault = requireNote(error);
    if (!vault) return false;
    QByteArray bytes;
    QString text;
    if (!vault->readNote(path_, &bytes, error) || !decode(bytes, &text, error)) return false;
    load(path_, bytes, text);
    return true;
}

bool Session::overwriteDisk(QString *error)
{
    if (!requireNote(error)) return false;
    if (!conflictBytes_) {
        *error = QStringLiteral("No outside change to replace.");
        return false;
    }
    return write(*conflictBytes_, error);
}

bool Session::mergeOutsideChanges(QString *error)
{
    const Vault *vault = requireNote(error);
    if (!vault) return false;
    if (!conflictBytes_ || !dirty()) {
        *error = QStringLiteral("No unsaved edits and reported outside change to merge.");
        return false;
    }
    QByteArray current;
    QString outside;
    if (!vault->readNote(path_, &current, error) || !decode(current, &outside, error)) return false;
    if (current != *conflictBytes_) {
        *error = QStringLiteral("Outside note changed again; refresh before merging.");
        return false;
    }
    const qsizetype baseLines = linesOf(savedContent_).size();
    const qsizetype mineLines = linesOf(content_).size();
    const qsizetype outsideLines = linesOf(outside).size();
    // The line diff uses a quadratic LCS table for each side. Refuse a merge
    // too large to compute interactively rather than exhausting memory.
    constexpr qsizetype maxCells = 2000000;
    if (baseLines > maxCells / mineLines || baseLines > maxCells / outsideLines) {
        *error = QStringLiteral("Note is too large for the line merge; edits are unchanged.");
        return false;
    }
    const QString merged = threeWayMerge(savedContent_, content_, outside);
    savedBytes_ = current;
    savedContent_ = outside;
    hasBom_ = current.startsWith("\xEF\xBB\xBF");
    content_ = merged;
    conflictBytes_.reset();
    return true;
}

bool Session::saveAsNewNote(QString *error)
{
    const Vault *vault = requireNote(error);
    if (!vault) return false;
    const QFileInfo target(notePath());
    if (target.exists() || target.isSymLink()) {
        *error = QStringLiteral("Note still exists; Save as new note cannot replace it.");
        return false;
    }
    const QByteArray bytes =
        (hasBom_ ? QByteArray::fromHex("efbbbf") : QByteArray()) + content_.toUtf8();
    if (!vault->createNoteWithContent(path_, bytes, error)) return false;
    savedContent_ = content_;
    savedBytes_ = bytes;
    conflictBytes_.reset();
    QString listError;
    reloadNotes(&listError);
    return true;
}

bool Session::createNote(const QString &input, QString *error)
{
    if (!requireClean(error)) return false;
    if (!vault_) {
        *error = QStringLiteral("Open a vault first.");
        return false;
    }
    // macOS lists names composed, so decomposed input would not match the list.
    QString path = input.trimmed().normalized(QString::NormalizationForm_C);
    if (!path.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive)) path += QStringLiteral(".md");
    if (!vault_->createNote(path, error) || !reloadNotes(error)) return false;
    // A case-insensitive file system may have kept an existing folder's spelling.
    if (!notes_.contains(path)) {
        for (const QString &note : notes_) {
            if (note.compare(path, Qt::CaseInsensitive) == 0) {
                path = note;
                break;
            }
        }
    }
    return selectNote(path, error);
}

bool Session::trashNote(QString *error)
{
    const Vault *vault = requireNote(error);
    if (!vault || !requireClean(error) || !vault->trashNote(path_, trash_, error)) return false;
    // The note is gone either way; a failed relist only drops it from the list.
    QString ignored;
    if (!reloadNotes(&ignored)) notes_.removeAll(path_);
    clearNote();
    return true;
}

bool Session::reloadNotes(QString *error)
{
    if (!vault_) return false;
    QStringList notes;
    QStringList folders;
    if (!vault_->listNotes(&notes, error, &folders)) return false;
    notes_ = notes;
    folders_ = folders;
    return true;
}

void Session::clearNote()
{
    path_.clear();
    content_.clear();
    savedContent_.clear();
    savedBytes_.clear();
    conflictBytes_.reset();
    hasBom_ = false;
}

bool Session::requireClean(QString *error) const
{
    if (!dirty()) return true;
    *error = QStringLiteral("Save or discard your edits first.");
    return false;
}

const Vault *Session::requireNote(QString *error) const
{
    if (vault_ && !path_.isEmpty()) return &*vault_;
    *error = QStringLiteral("Select a note first.");
    return nullptr;
}

QString Session::notePath() const
{
    if (!vault_ || path_.isEmpty()) return QString();
    return QDir(vault_->root()).filePath(path_);
}

bool Session::decode(const QByteArray &bytes, QString *text, QString *error)
{
    QStringDecoder decoder(QStringDecoder::Utf8);
    *text = decoder.decode(bytes);
    if (decoder.hasError()) {
        *error = QStringLiteral("Note is not valid UTF-8; refusing to risk changing its bytes.");
        return false;
    }
    return true;
}

void Session::load(const QString &path, const QByteArray &bytes, const QString &text)
{
    path_ = path;
    hasBom_ = bytes.startsWith("\xEF\xBB\xBF");
    content_ = text;
    savedContent_ = text;
    savedBytes_ = bytes;
    conflictBytes_.reset();
}
