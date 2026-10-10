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

QStringList LinesOf(const QString &text)
{
    return text.split(u'\n', Qt::KeepEmptyParts);
}

std::vector<Change> Changes(const QStringList &base, const QStringList &other)
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

bool Overlaps(const Change &a, const Change &b)
{
    if (a.mStart == a.mEnd && b.mStart == b.mEnd) return a.mStart == b.mStart;
    if (a.mStart == a.mEnd) return b.mStart <= a.mStart && a.mStart < b.mEnd;
    if (b.mStart == b.mEnd) return a.mStart <= b.mStart && b.mStart < a.mEnd;
    return a.mStart < b.mEnd && b.mStart < a.mEnd;
}

QStringList Apply(
    const QStringList &base, qsizetype start, qsizetype end, const std::vector<Change> &hunks)
{
    QStringList result;
    qsizetype pos = start;
    for (const Change &hunk : hunks) {
        for (; pos < hunk.mStart; ++pos)
            result.append(base[pos]);
        result.append(hunk.mLines);
        pos = hunk.mEnd;
    }
    for (; pos < end; ++pos)
        result.append(base[pos]);
    return result;
}

QString ThreeWayMerge(const QString &baseText, const QString &mineText, const QString &outsideText)
{
    const QStringList base = LinesOf(baseText);
    const auto mine = Changes(base, LinesOf(mineText));
    const auto outside = Changes(base, LinesOf(outsideText));
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
                                       [&](const Change &c) { return Overlaps(mine[a], c); }))
            {
                left.push_back(mine[a++]);
                expanded = true;
            }
            if (b < outside.size() && std::any_of(
                                          left.begin(),
                                          left.end(),
                                          [&](const Change &c) { return Overlaps(outside[b], c); }))
            {
                right.push_back(outside[b++]);
                expanded = true;
            }
        }
        qsizetype end = start;
        for (const Change &c : left)
            end = std::max(end, c.mEnd);
        for (const Change &c : right)
            end = std::max(end, c.mEnd);
        const QStringList l = Apply(base, start, end, left);
        const QStringList r = Apply(base, start, end, right);
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

bool Session::OpenVault(const QString &folder, QString *error)
{
    if (Dirty()) {
        *error = QStringLiteral("Save the current note before opening another vault.");
        return false;
    }
    auto next = Vault::Open(folder, error);
    if (!next) return false;
    QStringList notes;
    QStringList folders;
    if (!next->ListNotes(&notes, error, &folders)) return false;
    mVault = std::move(next);
    mNotes = notes;
    mFolders = folders;
    ClearNote();
    return true;
}

bool Session::SelectNote(const QString &path, QString *error)
{
    if (Dirty()) {
        *error = QStringLiteral("Save the current note before switching.");
        return false;
    }
    if (!mVault || !mNotes.contains(path)) {
        *error = QStringLiteral("Note is not in the current vault.");
        return false;
    }
    QByteArray bytes;
    if (!mVault->ReadNote(path, &bytes, error)) return false;
    QString text;
    if (!Decode(bytes, &text, error)) return false;
    Load(path, bytes, text);
    return true;
}

bool Session::Save(QString *error)
{
    return Write(mSavedBytes, error);
}

bool Session::Write(const QByteArray &expected, QString *error)
{
    const Vault *vault = RequireNote(error);
    if (!vault) return false;
    const QByteArray bytes =
        (mHasBom ? QByteArray::fromHex("efbbbf") : QByteArray()) + mContent.toUtf8();
    if (!vault->SaveNote(mPath, expected, bytes, error)) return false;
    mSavedContent = mContent;
    mSavedBytes = bytes;
    mConflictBytes.reset();
    return true;
}

DiskChange Session::SyncWithDisk(QString *error)
{
    if (!mVault || mPath.isEmpty()) return DiskChange::None;
    QByteArray bytes;
    if (!mVault->ReadNote(mPath, &bytes, error)) return DiskChange::Missing;
    if (bytes == mSavedBytes) return DiskChange::None;
    mConflictBytes = bytes;
    if (Dirty()) {
        *error = QStringLiteral(
            "Note changed on disk while you have unsaved edits; "
            "reload from disk or keep your version.");
        return DiskChange::Conflict;
    }
    QString text;
    if (!Decode(bytes, &text, error)) return DiskChange::Conflict;
    Load(mPath, bytes, text);
    return DiskChange::Reloaded;
}

bool Session::ReloadFromDisk(QString *error)
{
    const Vault *vault = RequireNote(error);
    if (!vault) return false;
    QByteArray bytes;
    QString text;
    if (!vault->ReadNote(mPath, &bytes, error) || !Decode(bytes, &text, error)) return false;
    Load(mPath, bytes, text);
    return true;
}

bool Session::OverwriteDisk(QString *error)
{
    if (!RequireNote(error)) return false;
    if (!mConflictBytes) {
        *error = QStringLiteral("No outside change to replace.");
        return false;
    }
    return Write(*mConflictBytes, error);
}

bool Session::MergeOutsideChanges(QString *error)
{
    const Vault *vault = RequireNote(error);
    if (!vault) return false;
    if (!mConflictBytes || !Dirty()) {
        *error = QStringLiteral("No unsaved edits and reported outside change to merge.");
        return false;
    }
    QByteArray current;
    QString outside;
    if (!vault->ReadNote(mPath, &current, error) || !Decode(current, &outside, error)) return false;
    if (current != *mConflictBytes) {
        *error = QStringLiteral("Outside note changed again; refresh before merging.");
        return false;
    }
    const qsizetype baseLines = LinesOf(mSavedContent).size();
    const qsizetype mineLines = LinesOf(mContent).size();
    const qsizetype outsideLines = LinesOf(outside).size();
    // The line diff uses a quadratic LCS table for each side. Refuse a merge
    // too large to compute interactively rather than exhausting memory.
    constexpr qsizetype kMaxCells = 2000000;
    if (baseLines > kMaxCells / mineLines || baseLines > kMaxCells / outsideLines) {
        *error = QStringLiteral("Note is too large for the line merge; edits are unchanged.");
        return false;
    }
    const QString merged = ThreeWayMerge(mSavedContent, mContent, outside);
    mSavedBytes = current;
    mSavedContent = outside;
    mHasBom = current.startsWith("\xEF\xBB\xBF");
    mContent = merged;
    mConflictBytes.reset();
    return true;
}

bool Session::SaveAsNewNote(QString *error)
{
    const Vault *vault = RequireNote(error);
    if (!vault) return false;
    const QFileInfo target(NotePath());
    if (target.exists() || target.isSymLink()) {
        *error = QStringLiteral("Note still exists; Save as new note cannot replace it.");
        return false;
    }
    const QByteArray bytes =
        (mHasBom ? QByteArray::fromHex("efbbbf") : QByteArray()) + mContent.toUtf8();
    if (!vault->CreateNoteWithContent(mPath, bytes, error)) return false;
    mSavedContent = mContent;
    mSavedBytes = bytes;
    mConflictBytes.reset();
    QString listError;
    ReloadNotes(&listError);
    return true;
}

bool Session::CreateNote(const QString &input, QString *error)
{
    if (!RequireClean(error)) return false;
    if (!mVault) {
        *error = QStringLiteral("Open a vault first.");
        return false;
    }
    // macOS lists names composed, so decomposed input would not match the list.
    QString path = input.trimmed().normalized(QString::NormalizationForm_C);
    if (!path.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive)) path += QStringLiteral(".md");
    if (!mVault->CreateNote(path, error) || !ReloadNotes(error)) return false;
    // A case-insensitive file system may have kept an existing folder's spelling.
    if (!mNotes.contains(path)) {
        for (const QString &note : mNotes) {
            if (note.compare(path, Qt::CaseInsensitive) == 0) {
                path = note;
                break;
            }
        }
    }
    return SelectNote(path, error);
}

bool Session::TrashNote(QString *error)
{
    const Vault *vault = RequireNote(error);
    if (!vault || !RequireClean(error) || !vault->TrashNote(mPath, mTrash, error)) return false;
    // The note is gone either way; a failed relist only drops it from the list.
    QString ignored;
    if (!ReloadNotes(&ignored)) mNotes.removeAll(mPath);
    ClearNote();
    return true;
}

bool Session::ReloadNotes(QString *error)
{
    if (!mVault) return false;
    QStringList notes;
    QStringList folders;
    if (!mVault->ListNotes(&notes, error, &folders)) return false;
    mNotes = notes;
    mFolders = folders;
    return true;
}

void Session::ClearNote()
{
    mPath.clear();
    mContent.clear();
    mSavedContent.clear();
    mSavedBytes.clear();
    mConflictBytes.reset();
    mHasBom = false;
}

bool Session::RequireClean(QString *error) const
{
    if (!Dirty()) return true;
    *error = QStringLiteral("Save or discard your edits first.");
    return false;
}

const Vault *Session::RequireNote(QString *error) const
{
    if (mVault && !mPath.isEmpty()) return &*mVault;
    *error = QStringLiteral("Select a note first.");
    return nullptr;
}

QString Session::NotePath() const
{
    if (!mVault || mPath.isEmpty()) return QString();
    return QDir(mVault->Root()).filePath(mPath);
}

bool Session::Decode(const QByteArray &bytes, QString *text, QString *error)
{
    QStringDecoder decoder(QStringDecoder::Utf8);
    *text = decoder.decode(bytes);
    if (decoder.hasError()) {
        *error = QStringLiteral("Note is not valid UTF-8; refusing to risk changing its bytes.");
        return false;
    }
    return true;
}

void Session::Load(const QString &path, const QByteArray &bytes, const QString &text)
{
    mPath = path;
    mHasBom = bytes.startsWith("\xEF\xBB\xBF");
    mContent = text;
    mSavedContent = text;
    mSavedBytes = bytes;
    mConflictBytes.reset();
}
