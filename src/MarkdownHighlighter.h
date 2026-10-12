#pragma once

#include <QSyntaxHighlighter>

#include <vector>

// Styles a note's Markdown as it is typed: ATX headings, bold, italic, inline
// code, fenced code blocks, wikilinks and frontmatter at the start of the note.
// It only formats the layout; no markup is hidden and the text never changes.
// Works line by line, carrying fenced code and frontmatter state to the next
// line, so an edit re-highlights only the lines whose state it changed.
class MarkdownHighlighter : public QSyntaxHighlighter
{
public:
    explicit MarkdownHighlighter(QTextDocument *document);

protected:
    void highlightBlock(const QString &text) override;

private:
    void continueFrontmatter(const QString &text);
    void continueFence(const QString &text, int state);
    void highlightInline(const QString &text);
    void highlightEmphasis(const QString &plain, const std::vector<bool> &inCode);
    // Adds a format to the existing formatting of length characters from
    // start, skipping characters marked in skip, if given.
    void layer(
        int start,
        int length,
        const QTextCharFormat &added,
        const std::vector<bool> *skip = nullptr);
};
