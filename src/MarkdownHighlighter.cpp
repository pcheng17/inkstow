#include "MarkdownHighlighter.h"

#include "Theme.h"

#include <QRegularExpression>
#include <QTextBlock>

#include <algorithm>
#include <array>
#include <optional>

namespace
{
using theme::MarkdownKind;

// Block states. A fenced code block records its fence character and length,
// so only a matching fence closes it.
constexpr int kNormal = 0;
constexpr int kFrontmatter = 1;
constexpr int kFenceFlag = 1 << 20;
constexpr int kTildeFlag = 1 << 19;
constexpr int kFenceLengthMask = kTildeFlag - 1;

// Stands in for text that later rules must not see, such as code spans.
constexpr QChar kMask(0xFFFC);

struct Fence
{
    QChar mMarker;
    int mLength;
};

struct Span
{
    int mStart;
    int mLength;
};

// Opener must be followed, and closer preceded, by a non-space. A * run must
// not touch another *, and a _ run must not touch a word character, so
// snake_case stays plain.
struct EmphasisRule
{
    QRegularExpression mPattern;
    bool mBold;
    bool mItalic;
};

int fenceState(Fence fence)
{
    const int tilde = fence.mMarker == u'~' ? kTildeFlag : 0;
    return kFenceFlag | tilde | std::min(fence.mLength, kFenceLengthMask);
}

bool isFenceState(int state)
{
    return state > 0 && (state & kFenceFlag) != 0;
}

Fence fenceFromState(int state)
{
    const QChar marker = (state & kTildeFlag) != 0 ? QChar(u'~') : QChar(u'`');
    return {marker, state & kFenceLengthMask};
}

// Obsidian only accepts --- around frontmatter, not YAML's ... end marker.
bool isFrontmatterDelimiter(const QString &text)
{
    static const QRegularExpression pattern(QStringLiteral(R"(^---[ \t]*$)"));
    return pattern.match(text).hasMatch();
}

// Up to three spaces, three or more ` or ~, then an info string, which may
// not hold a backtick after a backtick fence.
std::optional<Fence> openingFence(const QString &text)
{
    static const QRegularExpression pattern(QStringLiteral(R"(^ {0,3}(`{3,}|~{3,})(.*)$)"));
    const QRegularExpressionMatch match = pattern.match(text);
    if (!match.hasMatch()) return std::nullopt;
    const QChar marker = match.capturedView(1).front();
    if (marker == u'`' && match.capturedView(2).contains(u'`')) return std::nullopt;
    return Fence{marker, static_cast<int>(match.capturedLength(1))};
}

// A closing fence uses the same character, at least as many times, with
// nothing after it but whitespace.
bool closesFence(const QString &text, Fence fence)
{
    static const QRegularExpression pattern(QStringLiteral(R"(^ {0,3}(`{3,}|~{3,})[ \t]*$)"));
    const QRegularExpressionMatch match = pattern.match(text);
    return match.hasMatch() && match.capturedView(1).front() == fence.mMarker &&
           match.capturedLength(1) >= fence.mLength;
}

int backtickRun(const QString &text, int from)
{
    int end = from;
    while (end < text.size() && text[end] == u'`')
        ++end;
    return end - from;
}

// Start of the next run of exactly length backticks, or -1.
int findBacktickRun(const QString &text, int from, int length)
{
    for (int at = from; at < text.size();) {
        if (text[at] != u'`') {
            ++at;
            continue;
        }
        const int run = backtickRun(text, at);
        if (run == length) return at;
        at += run;
    }
    return -1;
}

// A run of backticks opens a code span that closes at the next run of the
// same length; a run with no match is literal text.
std::vector<Span> codeSpans(const QString &text)
{
    std::vector<Span> spans;
    for (int at = 0; at < text.size();) {
        if (text[at] != u'`') {
            ++at;
            continue;
        }
        const int run = backtickRun(text, at);
        const int close = findBacktickRun(text, at + run, run);
        if (close < 0) {
            at += run;
            continue;
        }
        spans.push_back({at, close + run - at});
        at = close + run;
    }
    return spans;
}

QString masked(QString text, const std::vector<Span> &spans)
{
    for (const Span &span : spans)
        std::fill_n(text.begin() + span.mStart, span.mLength, kMask);
    return text;
}

std::vector<Span> matches(const QRegularExpression &pattern, const QString &text)
{
    std::vector<Span> spans;
    QRegularExpressionMatchIterator found = pattern.globalMatch(text);
    while (found.hasNext()) {
        const QRegularExpressionMatch match = found.next();
        spans.push_back(
            {static_cast<int>(match.capturedStart()), static_cast<int>(match.capturedLength())});
    }
    return spans;
}

// The ! of an embed stays plain; only the [[...]] is a link.
const QRegularExpression &wikilinkPattern()
{
    static const QRegularExpression pattern(QStringLiteral(R"(\[\[[^\[\]\x{FFFC}]+\]\])"));
    return pattern;
}

QRegularExpression emphasisPattern(QChar marker, int count)
{
    const QString escaped = QRegularExpression::escape(QString(marker));
    const QString run = QStringLiteral("%1{%2}").arg(escaped).arg(count);
    const bool star = marker == u'*';
    const QString before = star ? QStringLiteral(R"((?<!\*))") : QStringLiteral(R"((?<!\w))");
    const QString after = star ? QStringLiteral(R"((?!\*))") : QStringLiteral(R"((?!\w))");
    const QString notSpace = QStringLiteral(R"([\s%1])").arg(escaped);
    return QRegularExpression(
        before + run + QStringLiteral("(?!%1)(.+?)(?<!%1)").arg(notSpace) + run + after,
        QRegularExpression::UseUnicodePropertiesOption);
}

// Longest runs first, so ***x*** is bold and italic rather than a stray *.
const std::array<EmphasisRule, 6> &emphasisRules()
{
    static const std::array<EmphasisRule, 6> rules{
        {
         {emphasisPattern(u'*', 3), true, true},
         {emphasisPattern(u'_', 3), true, true},
         {emphasisPattern(u'*', 2), true, false},
         {emphasisPattern(u'_', 2), true, false},
         {emphasisPattern(u'*', 1), false, true},
         {emphasisPattern(u'_', 1), false, true},
         }
    };
    return rules;
}
} // namespace

MarkdownHighlighter::MarkdownHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document)
{
    setObjectName(QStringLiteral("markdownHighlighter"));
}

void MarkdownHighlighter::highlightBlock(const QString &text)
{
    const int previous = previousBlockState();
    if (previous == kFrontmatter) {
        continueFrontmatter(text);
        return;
    }
    if (isFenceState(previous)) {
        continueFence(text, previous);
        return;
    }
    setCurrentBlockState(kNormal);
    // Frontmatter only opens on the note's very first line.
    if (!currentBlock().previous().isValid() && isFrontmatterDelimiter(text)) {
        formatLine(text, theme::markdownFormat(MarkdownKind::Frontmatter));
        setCurrentBlockState(kFrontmatter);
        return;
    }
    if (const std::optional<Fence> fence = openingFence(text)) {
        formatLine(text, theme::markdownFormat(MarkdownKind::CodeBlock));
        setCurrentBlockState(fenceState(*fence));
        return;
    }
    highlightInline(text);
}

void MarkdownHighlighter::continueFrontmatter(const QString &text)
{
    formatLine(text, theme::markdownFormat(MarkdownKind::Frontmatter));
    setCurrentBlockState(isFrontmatterDelimiter(text) ? kNormal : kFrontmatter);
}

void MarkdownHighlighter::continueFence(const QString &text, int state)
{
    formatLine(text, theme::markdownFormat(MarkdownKind::CodeBlock));
    setCurrentBlockState(closesFence(text, fenceFromState(state)) ? kNormal : state);
}

// A heading styles its whole line; code spans, then wikilinks, then emphasis
// layer on top. Text inside code spans is hidden from the later rules, and
// text inside wikilinks from emphasis.
void MarkdownHighlighter::highlightInline(const QString &text)
{
    static const QRegularExpression heading(QStringLiteral(R"(^ {0,3}(#{1,6})(?:[ \t]|$))"));
    const QRegularExpressionMatch match = heading.match(text);
    if (match.hasMatch()) {
        const int level = static_cast<int>(match.capturedLength(1));
        formatLine(text, theme::markdownFormat(MarkdownKind::Heading, level));
    }
    const std::vector<Span> code = codeSpans(text);
    std::vector<bool> inCode(static_cast<std::size_t>(text.size()), false);
    for (const Span &span : code) {
        layer(span.mStart, span.mLength, theme::markdownFormat(MarkdownKind::InlineCode));
        std::fill_n(inCode.begin() + span.mStart, span.mLength, true);
    }
    const QString withoutCode = masked(text, code);
    const std::vector<Span> links = matches(wikilinkPattern(), withoutCode);
    for (const Span &span : links)
        layer(span.mStart, span.mLength, theme::markdownFormat(MarkdownKind::Wikilink));
    highlightEmphasis(masked(withoutCode, links), inCode);
}

// Emphasis spans may contain code spans but do not style them.
void MarkdownHighlighter::highlightEmphasis(const QString &plain, const std::vector<bool> &inCode)
{
    const QTextCharFormat bold = theme::markdownFormat(MarkdownKind::Bold);
    const QTextCharFormat italic = theme::markdownFormat(MarkdownKind::Italic);
    for (const EmphasisRule &rule : emphasisRules()) {
        for (const Span &span : matches(rule.mPattern, plain)) {
            if (rule.mBold) layer(span.mStart, span.mLength, bold, &inCode);
            if (rule.mItalic) layer(span.mStart, span.mLength, italic, &inCode);
        }
    }
}

void MarkdownHighlighter::formatLine(const QString &text, const QTextCharFormat &format)
{
    setFormat(0, static_cast<int>(text.size()), format);
}

void MarkdownHighlighter::layer(
    int start, int length, const QTextCharFormat &added, const std::vector<bool> *skip)
{
    const int kinds = added.intProperty(theme::kMarkdownKindsProperty);
    for (int at = start; at < start + length; ++at) {
        if (skip && (*skip)[static_cast<std::size_t>(at)]) continue;
        QTextCharFormat merged = format(at);
        const int existing = merged.intProperty(theme::kMarkdownKindsProperty);
        merged.merge(added);
        merged.setProperty(theme::kMarkdownKindsProperty, existing | kinds);
        setFormat(at, 1, merged);
    }
}
