#pragma once

#include <QMargins>
#include <QRgb>

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

// Fonts.
constexpr int kTitlePointSize = 14;

// Spacing.
constexpr int kEmptyStateSpacing = 12;
constexpr QMargins kFlushMargins{};

// Window geometry.
constexpr int kWindowWidth = 1100;
constexpr int kWindowHeight = 750;
constexpr int kSidebarWidth = 240;
constexpr int kEditorWidth = kWindowWidth - kSidebarWidth;

// Returns base restyled as the app title.
QFont titleFont(QFont base);
} // namespace theme

void configureLightTheme(QApplication &app);
