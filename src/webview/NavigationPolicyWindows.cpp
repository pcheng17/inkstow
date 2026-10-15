#include "NavigationPolicy.h"

#include <WebView2.h>
#include <wrl.h>

#include <memory>
#include <stdexcept>

namespace inkstow::webview_shell
{
struct NavigationPolicy::WindowsEvents
{
    Microsoft::WRL::ComPtr<ICoreWebView2> mView;
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
    auto events = std::make_unique<WindowsEvents>();
    // webview 0.12 returns the controller, not the ICoreWebView2 event source.
    // get_CoreWebView2 supplies a reference held until all hooks are removed.
    auto* controller = static_cast<ICoreWebView2Controller*>(mBrowser);
    if (FAILED(controller->get_CoreWebView2(events->mView.GetAddressOf())) || !events->mView) {
        throw std::runtime_error("Could not obtain WebView2 navigation event source");
    }
    auto* view = events->mView.Get();
    // Do not register the binding or load HTML unless every event hook succeeds.
    auto navigation = Microsoft::WRL::Callback<ICoreWebView2NavigationStartingEventHandler>(
        [this](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
            LPWSTR uri = nullptr;
            if (FAILED(args->get_Uri(&uri))) {
                return args->put_Cancel(TRUE);
            }
            // WebView2 reports NavigateToString as a base64 data URL.
            const bool allow = uri && mGate.allowInitialWebView2Document(uri);
            CoTaskMemFree(uri);
            return args->put_Cancel(allow ? FALSE : TRUE);
        });
    auto frame = Microsoft::WRL::Callback<ICoreWebView2NavigationStartingEventHandler>(
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
    if (FAILED(view->add_NavigationStarting(navigation.Get(), &events->mNavigation))) {
        throw std::runtime_error("Could not install WebView2 navigation policy");
    }
    if (FAILED(view->add_FrameNavigationStarting(frame.Get(), &events->mFrame))) {
        view->remove_NavigationStarting(events->mNavigation);
        throw std::runtime_error("Could not install WebView2 frame policy");
    }
    if (FAILED(view->add_NewWindowRequested(newWindow.Get(), &events->mNewWindow))) {
        view->remove_FrameNavigationStarting(events->mFrame);
        view->remove_NavigationStarting(events->mNavigation);
        throw std::runtime_error("Could not install WebView2 popup policy");
    }
    mEvents = events.release();
}

NavigationPolicy::~NavigationPolicy()
{
    auto* view = mEvents->mView.Get();
    view->remove_NewWindowRequested(mEvents->mNewWindow);
    view->remove_FrameNavigationStarting(mEvents->mFrame);
    view->remove_NavigationStarting(mEvents->mNavigation);
    delete mEvents;
}
} // namespace inkstow::webview_shell
