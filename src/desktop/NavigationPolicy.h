#pragma once

#include <string_view>

namespace inkstow::webview_shell
{
// The embedded page is the only permitted document. Install the native policy
// before loading it; every later navigation (including another about:blank) fails.
// WebView2 reports NavigateToString as a base64 data URL rather than about:blank.
class NavigationGate
{
public:
    bool allowInitialDocument(std::string_view uri)
    {
        if (mInitialDocumentAllowed || uri != "about:blank") return false;
        mInitialDocumentAllowed = true;
        return true;
    }

    bool allowInitialWebView2Document(std::wstring_view uri)
    {
        constexpr std::wstring_view kPrefix = L"data:text/html;charset=utf-8;base64,";
        if (mInitialDocumentAllowed || uri.substr(0, kPrefix.size()) != kPrefix ||
            uri.size() == kPrefix.size())
        {
            return false;
        }
        mInitialDocumentAllowed = true;
        return true;
    }

private:
    bool mInitialDocumentAllowed = false;
};

class NavigationPolicy
{
public:
    explicit NavigationPolicy(void* browser);
    ~NavigationPolicy();
    NavigationPolicy(const NavigationPolicy&) = delete;
    NavigationPolicy& operator=(const NavigationPolicy&) = delete;
    NavigationPolicy(NavigationPolicy&&) = delete;
    NavigationPolicy& operator=(NavigationPolicy&&) = delete;

private:
    NavigationGate mGate;
    void* mBrowser = nullptr;
#ifdef __APPLE__
    void* mDelegate = nullptr;
#elif defined(_WIN32)
    struct WindowsEvents;
    WindowsEvents* mEvents = nullptr;
#else
    unsigned long mSignal = 0;
#endif
};
} // namespace inkstow::webview_shell
