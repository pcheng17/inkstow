#include "MarkdownHighlighter.h"
#include "Theme.h"

#include <QGuiApplication>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>

#define CHECK(value)                                                                               \
    do {                                                                                           \
        if (!(value))                                                                              \
            throw std::runtime_error("Failed: " #value " at line " + std::to_string(__LINE__));    \
    } while (false)

namespace
{
constexpr int kHeading = static_cast<int>(theme::MarkdownKind::Heading);
constexpr int kBold = static_cast<int>(theme::MarkdownKind::Bold);
constexpr int kItalic = static_cast<int>(theme::MarkdownKind::Italic);
constexpr int kInlineCode = static_cast<int>(theme::MarkdownKind::InlineCode);
constexpr int kCodeBlock = static_cast<int>(theme::MarkdownKind::CodeBlock);
constexpr int kWikilink = static_cast<int>(theme::MarkdownKind::Wikilink);
constexpr int kFrontmatter = static_cast<int>(theme::MarkdownKind::Frontmatter);

// One formatted run in a block: which kinds of formatting it has, and the
// heading level when it is part of a heading.
struct Span
{
    int mStart;
    int mLength;
    int mKinds;
    int mLevel = 0;

    bool operator==(const Span &other) const
    {
        return mStart == other.mStart && mLength == other.mLength && mKinds == other.mKinds &&
               mLevel == other.mLevel;
    }
};

using Spans = std::vector<Span>;

Span run(int start, int length, int kinds, int level = 0)
{
    return {start, length, kinds, level};
}

// A document with a highlighter attached before the text arrives, so it is
// highlighted at once, as in the editor. Like the editor's, it has a layout:
// without one the document reports no changes and nothing is re-highlighted.
std::unique_ptr<QTextDocument> highlighted(const QString &text)
{
    auto document = std::make_unique<QTextDocument>();
    document->documentLayout();
    new MarkdownHighlighter(document.get());
    document->setPlainText(text);
    return document;
}

Spans spans(const QTextDocument &document, int blockNumber)
{
    const QTextBlock block = document.findBlockByNumber(blockNumber);
    CHECK(block.isValid());
    Spans found;
    for (const QTextLayout::FormatRange &range : block.layout()->formats()) {
        found.push_back(
            {range.start,
             range.length,
             range.format.intProperty(theme::kMarkdownKindsProperty),
             range.format.intProperty(theme::kHeadingLevelProperty)});
    }
    std::sort(found.begin(), found.end(), [](const Span &a, const Span &b) {
        return a.mStart < b.mStart;
    });
    return found;
}

// The whole of each listed block has exactly `kinds`.
void checkWholeBlocks(const QTextDocument &document, const std::vector<int> &blocks, int kinds)
{
    for (const int number : blocks) {
        const int length = static_cast<int>(document.findBlockByNumber(number).text().size());
        const Spans expected = length > 0 ? Spans{run(0, length, kinds)} : Spans{};
        if (spans(document, number) != expected)
            throw std::runtime_error("Unexpected formatting in block " + std::to_string(number));
    }
}

// No block has any formatting.
void checkPlain(const QTextDocument &document)
{
    for (int number = 0; number < document.blockCount(); ++number) {
        if (!spans(document, number).empty())
            throw std::runtime_error("Unexpected formatting in block " + std::to_string(number));
    }
}

void testHeadings()
{
    const auto document = highlighted(QStringLiteral(
        "# One\n## Two\n### Three\n#### Four\n##### Five\n###### Six\n"
        "#nospace\n####### Seven\n    # indented\n   # three spaces\n#\n#\tTab"));
    const QStringList lines = document->toPlainText().split(QLatin1Char('\n'));
    for (int level = 1; level <= 6; ++level) {
        const int length = static_cast<int>(lines[level - 1].size());
        CHECK((spans(*document, level - 1) == Spans{run(0, length, kHeading, level)}));
    }
    CHECK(spans(*document, 6).empty());
    CHECK(spans(*document, 7).empty());
    CHECK(spans(*document, 8).empty());
    CHECK((spans(*document, 9) == Spans{run(0, 17, kHeading, 1)}));
    CHECK((spans(*document, 10) == Spans{run(0, 1, kHeading, 1)}));
    CHECK((spans(*document, 11) == Spans{run(0, 5, kHeading, 1)}));
}

void testEmphasis()
{
    const auto document = highlighted(QStringLiteral(
        "a **bold** b\na __bold__ b\na *it* b\na _it_ b\nsnake_case_name\na * b\n"
        "* list item\n2 * 3 * 4\n*\n***both***\n**a *b* c**\n**unclosed\n** not bold **\n"
        "*x*\né_x_é"));
    CHECK((spans(*document, 0) == Spans{run(2, 8, kBold)}));
    CHECK((spans(*document, 1) == Spans{run(2, 8, kBold)}));
    CHECK((spans(*document, 2) == Spans{run(2, 4, kItalic)}));
    CHECK((spans(*document, 3) == Spans{run(2, 4, kItalic)}));
    for (int plain = 4; plain <= 8; ++plain)
        CHECK(spans(*document, plain).empty());
    CHECK((spans(*document, 9) == Spans{run(0, 10, kBold | kItalic)}));
    CHECK(
        (spans(*document, 10) ==
         Spans{run(0, 4, kBold), run(4, 3, kBold | kItalic), run(7, 4, kBold)}));
    CHECK(spans(*document, 11).empty());
    CHECK(spans(*document, 12).empty());
    CHECK((spans(*document, 13) == Spans{run(0, 3, kItalic)}));
    CHECK(spans(*document, 14).empty()); // Intraword _ counts non-ASCII letters too.
}

void testInlineCode()
{
    const auto document = highlighted(QStringLiteral(
        "a `code` b\n``a ` b``\na `unclosed\n`**not bold**`\n`[[x]]`\n``a` b\n"
        "**a `b` c**\n# a `b`"));
    CHECK((spans(*document, 0) == Spans{run(2, 6, kInlineCode)}));
    CHECK((spans(*document, 1) == Spans{run(0, 9, kInlineCode)}));
    CHECK(spans(*document, 2).empty());
    CHECK((spans(*document, 3) == Spans{run(0, 14, kInlineCode)}));
    CHECK((spans(*document, 4) == Spans{run(0, 7, kInlineCode)}));
    CHECK(spans(*document, 5).empty());
    CHECK(
        (spans(*document, 6) == Spans{run(0, 4, kBold), run(4, 3, kInlineCode), run(7, 4, kBold)}));
    CHECK(
        (spans(*document, 7) ==
         Spans{run(0, 4, kHeading, 1), run(4, 3, kHeading | kInlineCode, 1)}));
}

void testWikilinks()
{
    const auto document = highlighted(QStringLiteral(
        "see [[Note]] now\n[[Note|alias]]\n[[Note#Heading]]\n![[image.png]]\n[[unclosed\n"
        "[[a]] and [[b]]\n*see [[Note]]*\n[[a_b*c*]]\n# [[Note]]"));
    CHECK((spans(*document, 0) == Spans{run(4, 8, kWikilink)}));
    CHECK((spans(*document, 1) == Spans{run(0, 14, kWikilink)}));
    CHECK((spans(*document, 2) == Spans{run(0, 16, kWikilink)}));
    CHECK((spans(*document, 3) == Spans{run(1, 13, kWikilink)})); // The embed's ! stays plain.
    CHECK(spans(*document, 4).empty());
    CHECK((spans(*document, 5) == Spans{run(0, 5, kWikilink), run(10, 5, kWikilink)}));
    CHECK(
        (spans(*document, 6) ==
         Spans{run(0, 5, kItalic), run(5, 8, kItalic | kWikilink), run(13, 1, kItalic)}));
    CHECK((spans(*document, 7) == Spans{run(0, 10, kWikilink)}));
    CHECK(
        (spans(*document, 8) == Spans{run(0, 2, kHeading, 1), run(2, 8, kHeading | kWikilink, 1)}));
}

void testFencedCode()
{
    const auto document = highlighted(QStringLiteral(
        "before\n"
        "```cpp\n" // 1 opens
        "# not heading **x**\n"
        "~~~\n"            // 3 tildes: content
        "``` x\n"          // 4 info string: content
        "```\n"            // 5 closes
        "after **b**\n"    // 6
        "~~~\n"            // 7 tildes do not open a fence
        "# h\n"            // 8
        "````\n"           // 9 opens
        "```\n"            // 10 shorter: content
        "````` \n"         // 11 longer, trailing space: closes
        "    ```\n"        // 12 indented four spaces: not a fence
        "```a`b\n"         // 13 backtick in the info string: not a fence
        "   ```\n"         // 14 three spaces: opens
        "unclosed **x**\n" // 15
        "\n"               // 16
        "end"));           // 17
    const QStringList lines = document->toPlainText().split(QLatin1Char('\n'));
    for (const int code : {1, 2, 3, 4, 5, 9, 10, 11, 14, 15, 17}) {
        const int length = static_cast<int>(lines[code].size());
        CHECK((spans(*document, code) == Spans{run(0, length, kCodeBlock)}));
    }
    for (const int plain : {0, 7, 12, 13, 16})
        CHECK(spans(*document, plain).empty());
    CHECK((spans(*document, 6) == Spans{run(6, 5, kBold)}));
    CHECK((spans(*document, 8) == Spans{run(0, 3, kHeading, 1)}));
}

void testFrontmatter()
{
    // Closes on ---; a later --- is not frontmatter.
    const auto closed =
        highlighted(QStringLiteral("---\ntitle: **x**\ntags: [a]\n---\n# Heading\n---\nplain"));
    checkWholeBlocks(*closed, {0, 1, 2, 3}, kFrontmatter);
    CHECK((spans(*closed, 4) == Spans{run(0, 9, kHeading, 1)}));
    CHECK(spans(*closed, 5).empty());
    CHECK(spans(*closed, 6).empty());

    // Only a --- on the very first line opens frontmatter.
    checkPlain(*highlighted(QStringLiteral("\n---\na: b\n---")));
    checkPlain(*highlighted(QStringLiteral("text\n---\na: b\n---")));
    checkPlain(*highlighted(QStringLiteral("----\na: b\n---")));

    // Unclosed frontmatter runs to the end; ... does not close it.
    checkWholeBlocks(
        *highlighted(QStringLiteral("--- \nunclosed: yes\n# still")),
        {0, 1, 2},
        kFrontmatter);
    checkWholeBlocks(*highlighted(QStringLiteral("---\na\n...\nb")), {0, 1, 2, 3}, kFrontmatter);
    const auto empty = highlighted(QStringLiteral("---\n---\nafter"));
    checkWholeBlocks(*empty, {0, 1}, kFrontmatter);
    CHECK(spans(*empty, 2).empty());
}

// Edits re-highlight later lines whose state changed, and undoing the edit
// restores them.
void testEditingCarriesState()
{
    const auto document = highlighted(QStringLiteral("one\n**two**\nthree\n# four"));
    QTextCursor cursor(document->findBlockByNumber(1));
    cursor.insertText(QStringLiteral("```\n"));
    CHECK(spans(*document, 0).empty());
    checkWholeBlocks(*document, {1, 2, 3, 4}, kCodeBlock);
    cursor.setPosition(document->findBlockByNumber(1).position());
    cursor.setPosition(document->findBlockByNumber(2).position(), QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    CHECK(document->toPlainText() == QStringLiteral("one\n**two**\nthree\n# four"));
    CHECK(spans(*document, 0).empty());
    CHECK((spans(*document, 1) == Spans{run(0, 7, kBold)}));
    CHECK(spans(*document, 2).empty());
    CHECK((spans(*document, 3) == Spans{run(0, 6, kHeading, 1)}));

    const auto front = highlighted(QStringLiteral("a: b\n---\n**c**"));
    QTextCursor start(front.get());
    start.insertText(QStringLiteral("---\n"));
    checkWholeBlocks(*front, {0, 1, 2}, kFrontmatter);
    CHECK((spans(*front, 3) == Spans{run(0, 5, kBold)}));
    front->undo();
    CHECK(spans(*front, 0).empty());
    CHECK((spans(*front, 2) == Spans{run(0, 5, kBold)}));
}

// Highlighting only formats the layout: the text and undo history stay as they were.
void testTextUnchanged()
{
    const QString text = QStringLiteral(
        "---\ntitle: x\n---\n# Heading `code`\nSome **bold**, *italic*, __b__ and _i_.\n"
        "A [[Note|alias]] and ![[embed.png]] and [[unclosed\n```js\nlet a = `b`;\n```\n"
        "```\nunclosed fence\n");
    const auto document = highlighted(text);
    auto *highlighter = dynamic_cast<MarkdownHighlighter *>(
        document->findChild<QSyntaxHighlighter *>(QStringLiteral("markdownHighlighter")));
    CHECK(highlighter);
    document->setModified(false);
    highlighter->rehighlight();
    CHECK(document->toPlainText() == text);
    CHECK(!document->isModified());
    CHECK(!document->isUndoAvailable());
}

// Each kind of formatting has a distinct look taken from the theme.
void testFormatsFromTheme()
{
    using theme::MarkdownKind;
    CHECK(theme::markdownFormat(MarkdownKind::Bold).fontWeight() == QFont::Bold);
    CHECK(theme::markdownFormat(MarkdownKind::Italic).fontItalic());
    CHECK(theme::markdownFormat(MarkdownKind::InlineCode).fontFixedPitch());
    CHECK(theme::markdownFormat(MarkdownKind::CodeBlock).fontFixedPitch());
    CHECK(
        theme::markdownFormat(MarkdownKind::Wikilink).foreground().color() ==
        QColor(theme::kLinkInk));
    CHECK(
        theme::markdownFormat(MarkdownKind::Frontmatter).foreground().color() ==
        QColor(theme::kMutedInk));
    const QTextCharFormat first = theme::markdownFormat(MarkdownKind::Heading, 1);
    const QTextCharFormat sixth = theme::markdownFormat(MarkdownKind::Heading, 6);
    CHECK(first.fontPointSize() > sixth.fontPointSize());
    CHECK(first.intProperty(theme::kHeadingLevelProperty) == 1);
    CHECK(sixth.intProperty(theme::kHeadingLevelProperty) == 6);
    CHECK(first.intProperty(theme::kMarkdownKindsProperty) == kHeading);
}
} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    try {
        testHeadings();
        testEmphasis();
        testInlineCode();
        testWikilinks();
        testFencedCode();
        testFrontmatter();
        testEditingCarriesState();
        testTextUnchanged();
        testFormatsFromTheme();
    } catch (const std::exception &error) {
        qCritical("%s", error.what());
        return 1;
    }
    return 0;
}
