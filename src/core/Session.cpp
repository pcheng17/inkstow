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
    qsizetype mStart;
    qsizetype mEnd;
    QStringList mLines;
};

QStringList linesOf(const QString& text)
{
    return text.split(u'\n', Qt::KeepEmptyParts);
}

std::vector<Change> changes(const QStringList& base, const QStringList& other)
{
    const qsizetype n = base.size();
    const qsizetype m = other.size();
    std::vector<std::vector<int>> lcs(n + 1, std::vector<int>(m + 1));
    for (qsizetype i = n - 1; i >= 0; --i)
        for (qsizetype j = m - 1; j >= 0; --j)
            lcs[i][j] = base[i] == other[j] ? 1 + lcs[i + 1][j + 1]
                                            : std::max(lcs[i + 1][j], lcs[i][j + 1]);
    std::vector<Change> result;
    qsizetype i = 0;
    qsizetype j = 0;
    while (i < n || j < m) {
        if (i < n && j < m && base[i] == other[j]) {
            ++i;
            ++j;
            continue;
        }
        const qsizetype start = i;
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

bool overlaps(const Change& a, const Change& b)
{
    if (a.mStart == a.mEnd && b.mStart == b.mEnd) return a.mStart == b.mStart;
    if (a.mStart == a.mEnd) return b.mStart <= a.mStart && a.mStart < b.mEnd;
    if (b.mStart == b.mEnd) return a.mStart <= b.mStart && b.mStart < a.mEnd;
    return a.mStart < b.mEnd && b.mStart < a.mEnd;
}

QStringList apply(
    const QStringList& base, qsizetype start, qsizetype end, const std::vector<Change>& hunks)
{
    QStringList result;
    qsizetype pos = start;
    for (const Change& hunk : hunks) {
        for (; pos < hunk.mStart; ++pos)
            result.append(base[pos]);
        result.append(hunk.mLines);
        pos = hunk.mEnd;
    }
    for (; pos < end; ++pos)
        result.append(base[pos]);
    return result;
}

QString threeWayMerge(const QString& baseText, const QString& mineText, const QString& outsideText)
{
    const QStringList base = linesOf(baseText);
    const auto mine = changes(base, linesOf(mineText));
    const auto outside = changes(base, linesOf(outsideText));
    QStringList result;
    size_t a = 0;
    size_t b = 0;
    qsizetype pos = 0;
    while (a < mine.size() || b < outside.size()) {
        const qsizetype start = std::min(
            a < mine.size() ? mine[a].mStart : base.size(),
            b < outside.size() ? outside[b].mStart : base.size());
        for (; pos < start; ++pos)
            result.append(base[pos]);
        std::vector<Change> left;
        std::vector<Change> right;
        if (a < mine.size() && (b == outside.size() || mine[a].mStart <= outside[b].mStart))
            left.push_back(mine[a++]);
        else
            right.push_back(outside[b++]);
        bool expanded = true;
        while (expanded) {
            expanded = false;
            if (a < mine.size() && std::any_of(
                                       right.begin(),
                                       right.end(),
                                       [&](const Change& c) { return overlaps(mine[a], c); }))
            {
                left.push_back(mine[a++]);
                expanded = true;
            }
            if (b < outside.size() && std::any_of(
                                          left.begin(),
                                          left.end(),
                                          [&](const Change& c) { return overlaps(outside[b], c); }))
            {
                right.push_back(outside[b++]);
                expanded = true;
            }
        }
        qsizetype end = start;
        for (const Change& c : left)
            end = std::max(end, c.mEnd);
        for (const Change& c : right)
            end = std::max(end, c.mEnd);
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

bool Session::canOpenVault(QString* error) const
{
    if (dirty()) {
        *error = QStringLiteral("Save the current note before opening another vault.");
        return false;
    }
    return true;
}

bool Session::openVault(const QString& folder, QString* error)
{
    if (!canOpenVault(error)) return false;
    auto next = Vault::open(folder, error);
    if (!next) return false;
    QStringList notes;
    QStringList folders;
    if (!next->listNotes(&notes, error, &folders)) return false;
    mVault = std::move(next);
    mNotes = notes;
    mFolders = folders;
    clearNote();
    return true;
}

bool Session::selectNote(const QString& path, QString* error)
{
    if (dirty()) {
        *error = QStringLiteral("Save the current note before switching.");
        return false;
    }
    if (!mVault || !mNotes.contains(path)) {
        *error = QStringLiteral("Note is not in the current vault.");
        return false;
    }
    QByteArray bytes;
    if (!mVault->readNote(path, &bytes, error)) return false;
    QString text;
    if (!decode(bytes, &text, error)) return false;
    load(path, bytes, text);
    return true;
}

bool Session::save(QString* error)
{
    return write(mSavedBytes, error);
}

bool Session::write(const QByteArray& expected, QString* error)
{
    const Vault* vault = requireNote(error);
    if (!vault) return false;
    const QByteArray bytes = bytesForContent();
    if (!vault->saveNote(mPath, expected, bytes, error)) return false;
    mSavedContent = mContent;
    mSavedBytes = bytes;
    mConflictBytes.reset();
    return true;
}

DiskChange Session::syncWithDisk(QString* error)
{
    if (!mVault || mPath.isEmpty()) return DiskChange::None;
    QByteArray bytes;
    if (!mVault->readNote(mPath, &bytes, error)) return DiskChange::Missing;
    if (bytes == mSavedBytes) return DiskChange::None;
    mConflictBytes = bytes;
    if (dirty()) {
        *error = QStringLiteral(
            "Note changed on disk while you have unsaved edits; "
            "reload from disk or keep your version.");
        return DiskChange::Conflict;
    }
    QString text;
    if (!decode(bytes, &text, error)) return DiskChange::Conflict;
    load(mPath, bytes, text);
    return DiskChange::Reloaded;
}

bool Session::reloadFromDisk(QString* error)
{
    const Vault* vault = requireNote(error);
    if (!vault) return false;
    QByteArray bytes;
    QString text;
    if (!vault->readNote(mPath, &bytes, error) || !decode(bytes, &text, error)) return false;
    load(mPath, bytes, text);
    return true;
}

bool Session::overwriteDisk(QString* error)
{
    if (!requireNote(error)) return false;
    if (!mConflictBytes) {
        *error = QStringLiteral("No outside change to replace.");
        return false;
    }
    return write(*mConflictBytes, error);
}

bool Session::mergeOutsideChanges(QString* error)
{
    const Vault* vault = requireNote(error);
    if (!vault) return false;
    if (!mConflictBytes || !dirty()) {
        *error = QStringLiteral("No unsaved edits and reported outside change to merge.");
        return false;
    }
    QByteArray current;
    QString outside;
    if (!vault->readNote(mPath, &current, error) || !decode(current, &outside, error)) return false;
    if (current != *mConflictBytes) {
        *error = QStringLiteral("Outside note changed again; refresh before merging.");
        return false;
    }
    const QString outsideOnDisk = outside;
    outside.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    outside.replace(u'\r', u'\n');
    const qsizetype baseLines = linesOf(mSavedContent).size();
    const qsizetype mineLines = linesOf(mContent).size();
    const qsizetype outsideLines = linesOf(outside).size();
    // The line diff uses a quadratic LCS table for each side. Refuse a merge
    // too large to compute interactively rather than exhausting memory.
    constexpr qsizetype kMaxCells = 2000000;
    if (baseLines > kMaxCells / mineLines || baseLines > kMaxCells / outsideLines) {
        *error = QStringLiteral("Note is too large for the line merge; edits are unchanged.");
        return false;
    }
    const QString merged = threeWayMerge(mSavedContent, mContent, outside);
    rememberLineEnding(outsideOnDisk);
    mSavedBytes = current;
    mSavedContent = outside;
    mHasBom = current.startsWith("\xEF\xBB\xBF");
    mContent = merged;
    mConflictBytes.reset();
    return true;
}

bool Session::saveAsNewNote(QString* error)
{
    const Vault* vault = requireNote(error);
    if (!vault) return false;
    const QFileInfo target(notePath());
    if (target.exists() || target.isSymLink()) {
        *error = QStringLiteral("Note still exists; Save as new note cannot replace it.");
        return false;
    }
    const QByteArray bytes = bytesForContent();
    if (!vault->createNoteWithContent(mPath, bytes, error)) return false;
    mSavedContent = mContent;
    mSavedBytes = bytes;
    mConflictBytes.reset();
    QString listError;
    reloadNotes(&listError);
    return true;
}

bool Session::createNote(const QString& input, QString* error)
{
    if (!requireClean(error)) return false;
    if (!mVault) {
        *error = QStringLiteral("Open a vault first.");
        return false;
    }
    // macOS lists names composed, so decomposed input would not match the list.
    QString path = input.trimmed().normalized(QString::NormalizationForm_C);
    if (!path.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive)) path += QStringLiteral(".md");
    if (!mVault->createNote(path, error) || !reloadNotes(error)) return false;
    // A case-insensitive file system may have kept an existing folder's spelling.
    if (!mNotes.contains(path)) {
        for (const QString& note : mNotes) {
            if (note.compare(path, Qt::CaseInsensitive) == 0) {
                path = note;
                break;
            }
        }
    }
    return selectNote(path, error);
}

bool Session::moveNote(const QString& input, bool updateLinks, QString* error)
{
    const Vault* vault = requireNote(error);
    if (!vault || !requireClean(error)) return false;
    QString path = input.trimmed().normalized(QString::NormalizationForm_C);
    if (!path.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive)) path += QStringLiteral(".md");
    if (!vault->moveNote(mPath, path, mSavedBytes, updateLinks, error)) {
        if (error->startsWith(QStringLiteral("Move partially completed"))) {
            const QString failure = *error;
            QByteArray partial;
            QString text;
            if (vault->readNote(path, &partial, error) && decode(partial, &text, error)) {
                QString ignored;
                if (reloadNotes(&ignored)) path = discoveredPath(path);
                load(path, partial, text);
            }
            *error = failure;
        }
        return false;
    }
    // The selected note may itself contain rewritten links. Discovery may use
    // the existing folder's spelling on a case-insensitive filesystem.
    QString ignored;
    if (reloadNotes(&ignored)) path = discoveredPath(path);
    QByteArray bytes;
    QString text;
    if (!vault->readNote(path, &bytes, error) || !decode(bytes, &text, error)) {
        const QString failure = *error;
        mPath = path; // The move succeeded even if the new disk state cannot be loaded.
        *error = QStringLiteral("Move partially completed; check the moved note: ") + failure;
        return false;
    }
    load(path, bytes, text);
    return true;
}

QString Session::discoveredPath(const QString& requested) const
{
    for (const QString& note : mNotes) {
        if (note.compare(requested, Qt::CaseInsensitive) == 0) return note;
    }
    return requested;
}

bool Session::trashNote(QString* error)
{
    const Vault* vault = requireNote(error);
    if (!vault || !requireClean(error) || !vault->trashNote(mPath, mTrash, error)) return false;
    // The note is gone either way; a failed relist only drops it from the list.
    QString ignored;
    if (!reloadNotes(&ignored)) mNotes.removeAll(mPath);
    clearNote();
    return true;
}

bool Session::reloadNotes(QString* error)
{
    if (!mVault) return false;
    QStringList notes;
    QStringList folders;
    if (!mVault->listNotes(&notes, error, &folders)) return false;
    mNotes = notes;
    mFolders = folders;
    return true;
}

void Session::clearNote()
{
    mPath.clear();
    mContent.clear();
    mSavedContent.clear();
    mSavedBytes.clear();
    mConflictBytes.reset();
    mHasBom = false;
    mLineEnding = LineEnding::Lf;
}

bool Session::requireClean(QString* error) const
{
    if (!dirty()) return true;
    *error = QStringLiteral("Save or discard your edits first.");
    return false;
}

const Vault* Session::requireNote(QString* error) const
{
    if (mVault && !mPath.isEmpty()) return &*mVault;
    *error = QStringLiteral("Select a note first.");
    return nullptr;
}

QString Session::notePath() const
{
    if (!mVault || mPath.isEmpty()) return QString();
    return QDir(mVault->root()).filePath(mPath);
}

bool Session::decode(const QByteArray& bytes, QString* text, QString* error)
{
    QStringDecoder decoder(QStringDecoder::Utf8);
    *text = decoder.decode(bytes);
    if (decoder.hasError()) {
        *error = QStringLiteral("Note is not valid UTF-8; refusing to risk changing its bytes.");
        return false;
    }
    return true;
}

QByteArray Session::bytesForContent() const
{
    if (mContent == mSavedContent) return mSavedBytes;
    QString text = mContent;
    if (mLineEnding == LineEnding::CrLf)
        text.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
    else if (mLineEnding == LineEnding::Cr)
        text.replace(u'\n', u'\r');
    return (mHasBom ? QByteArray::fromHex("efbbbf") : QByteArray()) + text.toUtf8();
}

void Session::rememberLineEnding(const QString& text)
{
    // Mixed styles are normalized to LF only when edited; an unedited save
    // uses the original disk bytes. Uniform styles survive subsequent edits.
    const bool crlf = text.contains(QStringLiteral("\r\n"));
    QString withoutPairs = text;
    withoutPairs.replace(QStringLiteral("\r\n"), QString());
    const bool cr = withoutPairs.contains(u'\r');
    const bool lf = withoutPairs.contains(u'\n');
    mLineEnding = LineEnding::Lf;
    if (crlf && !cr && !lf) mLineEnding = LineEnding::CrLf;
    if (cr && !crlf && !lf) mLineEnding = LineEnding::Cr;
}

void Session::load(const QString& path, const QByteArray& bytes, const QString& text)
{
    mPath = path;
    mHasBom = bytes.startsWith("\xEF\xBB\xBF");
    rememberLineEnding(text);
    mContent = text;
    mContent.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    mContent.replace(u'\r', u'\n');
    mSavedContent = mContent;
    mSavedBytes = bytes;
    mConflictBytes.reset();
}
