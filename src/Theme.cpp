#include "Theme.h"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QPalette>
#include <QStyleFactory>

#include <algorithm>

QFont theme::titleFont(QFont base)
{
    base.setBold(true);
    base.setPointSize(kTitlePointSize);
    return base;
}

QFont theme::editorFont(QFont base)
{
    base.setPointSize(kEditorPointSize);
    return base;
}

namespace
{
QTextCharFormat monospace(QTextCharFormat format)
{
    format.setFontFamilies({QFontDatabase::systemFont(QFontDatabase::FixedFont).family()});
    format.setFontFixedPitch(true);
    format.setBackground(QColor(theme::kCodeBackground));
    return format;
}

QTextCharFormat heading(QTextCharFormat format, int level)
{
    const int clamped = std::clamp(level, 1, static_cast<int>(theme::kHeadingSizePercent.size()));
    const int percent = theme::kHeadingSizePercent.at(static_cast<std::size_t>(clamped - 1));
    format.setFontWeight(QFont::Bold);
    format.setFontPointSize(theme::kEditorPointSize * percent / 100.0);
    format.setProperty(theme::kHeadingLevelProperty, clamped);
    return format;
}
} // namespace

QTextCharFormat theme::markdownFormat(MarkdownKind kind, int headingLevel)
{
    QTextCharFormat format;
    format.setProperty(kMarkdownKindsProperty, static_cast<int>(kind));
    switch (kind) {
    case MarkdownKind::Heading:
        return heading(format, headingLevel);
    case MarkdownKind::Bold:
        format.setFontWeight(QFont::Bold);
        break;
    case MarkdownKind::Italic:
        format.setFontItalic(true);
        break;
    case MarkdownKind::InlineCode:
    case MarkdownKind::CodeBlock:
        return monospace(format);
    case MarkdownKind::Wikilink:
        format.setForeground(QColor(kLinkInk));
        break;
    case MarkdownKind::Frontmatter:
        format.setForeground(QColor(kMutedInk));
        break;
    }
    return format;
}

void configureLightTheme([[maybe_unused]] QApplication &app)
{
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette light;
    light.setColor(QPalette::Window, QColor(theme::kPaper));
    light.setColor(QPalette::WindowText, QColor(theme::kInk));
    light.setColor(QPalette::Base, QColor(theme::kSurface));
    light.setColor(QPalette::AlternateBase, QColor(theme::kPaper));
    light.setColor(QPalette::Text, QColor(theme::kInk));
    light.setColor(QPalette::Button, QColor(theme::kSurface));
    light.setColor(QPalette::ButtonText, QColor(theme::kInk));
    light.setColor(QPalette::Highlight, QColor(theme::kSelection));
    light.setColor(QPalette::HighlightedText, QColor(theme::kInk));
    QApplication::setPalette(light);
}
