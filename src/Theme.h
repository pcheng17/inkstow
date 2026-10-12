#pragma once

#include <QMargins>
#include <QRgb>
#include <QTextCharFormat>

#include <array>
#include <cstdint>

class QApplication;
class QFont;

// The single place for style values. Widgets read these instead of hard-coding
// colours, fonts, spacing or sizes, so a new look is applied here alone.
namespace theme
{
// Light palette.
constexpr QRgb kInk = qRgb(0x24, 0x21, 0x2b);
constexpr QRgb kPaper = qRgb(0xfa, 0xf9, 0xfc);
constexpr QRgb kSurface = qRgb(0xff, 0xff, 0xff);
constexpr QRgb kSelection = qRgb(0xe7, 0xde, 0xf4);
constexpr QRgb kMutedInk = qRgb(0x6f, 0x6a, 0x7a);        // Frontmatter.
constexpr QRgb kLinkInk = qRgb(0x5b, 0x3f, 0x9e);         // Wikilinks.
constexpr QRgb kCodeBackground = qRgb(0xf0, 0xed, 0xf5);  // Inline code and code blocks.
constexpr QRgb kUnsavedInk = kLinkInk;                    // The unsaved-changes indicator.
constexpr QRgb kErrorInk = qRgb(0x9b, 0x22, 0x30);        // Error status text.
constexpr QRgb kErrorBackground = qRgb(0xff, 0xeb, 0xed); // Error status surface.
constexpr QRgb kPromptBackground = kSelection;            // Attention prompts.

// Fonts.
constexpr int kTitlePointSize = 14;
constexpr int kEditorPointSize = 13;
constexpr int kEditorLineHeightPercent = 150; // Of the font's natural line height.
// Heading sizes by level, as a percentage of kEditorPointSize.
constexpr std::array<int, 6> kHeadingSizePercent{160, 140, 125, 115, 105, 100};

// Spacing.
constexpr int kEmptyStateSpacing = 12;   // Between the empty state's heading, message and button.
constexpr int kTreeIndent = 12;          // Per nesting level in the note tree.
constexpr int kHeaderSpacing = 8;        // Between the note title and the unsaved indicator.
constexpr int kEditorPadding = 24;       // Between note text and the editor's edges.
constexpr int kEditorMaxTextWidth = 720; // Longer lines wrap; wider editors centre the text.
constexpr QMargins kFlushMargins{};
constexpr QMargins kPromptMargins{12, 8, 12, 8};
constexpr int kPromptSpacing = 8;

// Window geometry.
constexpr int kWindowWidth = 1100;
constexpr int kWindowHeight = 750;
constexpr int kSidebarWidth = 240;
constexpr int kEditorWidth = kWindowWidth - kSidebarWidth;

// Returns base restyled as the app title.
QFont titleFont(QFont base);
// Returns base restyled for note text.
QFont editorFont(QFont base);

// Kinds of Markdown formatting. Each is its own bit so text styled several
// ways, such as bold inside a heading, can record all of them.
enum class MarkdownKind : std::uint8_t
{
    Heading = 1 << 0,
    Bold = 1 << 1,
    Italic = 1 << 2,
    InlineCode = 1 << 3,
    CodeBlock = 1 << 4,
    Wikilink = 1 << 5,
    Frontmatter = 1 << 6,
};

// Markdown formats record their kinds (MarkdownKind bits) and, for headings,
// the level 1-6 in these properties, so they can be told apart by meaning
// rather than by look.
constexpr int kMarkdownKindsProperty = QTextFormat::UserProperty + 1;
constexpr int kHeadingLevelProperty = QTextFormat::UserProperty + 2;

// Returns the editor format for one kind of Markdown; headingLevel (1-6) only
// applies to headings.
QTextCharFormat markdownFormat(MarkdownKind kind, int headingLevel = 1);
} // namespace theme

void configureLightTheme(QApplication &app);
