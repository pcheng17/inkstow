#pragma once

// Upstream's single-header implementation exports definitions in every translation unit
// with WEBVIEW_STATIC. Keep the additional native-shell TU's definitions inline.
#define WEBVIEW_API inline
#include "WebviewHeader.h"
#undef WEBVIEW_API

namespace inkstow::webview_shell
{
// Connects OS close and menu actions to the same editor operations used by controls.
class DesktopWindow
{
public:
    explicit DesktopWindow(webview::webview& window);
    DesktopWindow(const DesktopWindow&) = delete;
    DesktopWindow& operator=(const DesktopWindow&) = delete;
    DesktopWindow(DesktopWindow&&) = delete;
    DesktopWindow& operator=(DesktopWindow&&) = delete;
    ~DesktopWindow();
    void invoke(const char* script);
#if defined(_WIN32)
    void* previous() const;
#endif

private:
    webview::webview& mWindow;
#if defined(_WIN32)
    void* mHandle = nullptr;
    void* mPrevious = nullptr;
#elif defined(__APPLE__)
    void* mHandle = nullptr;
    void* mDelegate = nullptr;
#else
    void* mHandle = nullptr;
#endif
};
} // namespace inkstow::webview_shell
