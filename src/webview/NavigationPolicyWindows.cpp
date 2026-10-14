#include "NavigationPolicy.h"

#include <WebView2.h>
#include <wrl.h>

#include <stdexcept>

namespace inkstow::webview_shell
{
struct NavigationPolicy::WindowsEvents
{
    EventRegistrationToken mNavigation{};
    EventRegistrationToken mFrame{};
    EventRegistrationToken mNewWindow{};
};

NavigationPolicy::NavigationPolicy(void* browser)
    : mBrowser(browser)
{
    if (!mBrowser) {
        throw std::runtime_error("Webview browser handle is unavailable");
    }
    auto* view = static_cast<ICoreWebView2*>(mBrowser);
    // Do not register the binding or load HTML unless every event hook succeeds.
    auto navigation = Microsoft::WRL::Callback<ICoreWebView2NavigationStartingEventHandler>(
        [this](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
            LPWSTR uri = nullptr;
            if (FAILED(args->get_Uri(&uri))) {
                return args->put_Cancel(TRUE);
            }
            const bool allow = uri && std::wstring_view(uri) == L"about:blank" &&
                               mGate.allowInitialDocument("about:blank");
            CoTaskMemFree(uri);
            return args->put_Cancel(allow ? FALSE : TRUE);
        });
    auto frame = Microsoft::WRL::Callback<ICoreWebView2FrameNavigationStartingEventHandler>(
        [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
            return args->put_Cancel(TRUE);
        });
    auto newWindow = Microsoft::WRL::Callback<ICoreWebView2NewWindowRequestedEventHandler>(
        [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
            return args->put_Handled(TRUE);
        });
    if (!navigation || !frame || !newWindow) {
        throw std::runtime_error("Could not create WebView2 navigation policy");
    }
    auto* events = new WindowsEvents;
    if (FAILED(view->add_NavigationStarting(navigation.Get(), &events->mNavigation))) {
        delete events;
        throw std::runtime_error("Could not install WebView2 navigation policy");
    }
    if (FAILED(view->add_FrameNavigationStarting(frame.Get(), &events->mFrame))) {
        view->remove_NavigationStarting(events->mNavigation);
        delete events;
        throw std::runtime_error("Could not install WebView2 frame policy");
    }
    if (FAILED(view->add_NewWindowRequested(newWindow.Get(), &events->mNewWindow))) {
        view->remove_FrameNavigationStarting(events->mFrame);
        view->remove_NavigationStarting(events->mNavigation);
        delete events;
        throw std::runtime_error("Could not install WebView2 popup policy");
    }
    mEvents = events;
}

NavigationPolicy::~NavigationPolicy()
{
    auto* view = static_cast<ICoreWebView2*>(mBrowser);
    view->remove_NewWindowRequested(mEvents->mNewWindow);
    view->remove_FrameNavigationStarting(mEvents->mFrame);
    view->remove_NavigationStarting(mEvents->mNavigation);
    delete mEvents;
}
} // namespace inkstow::webview_shell
