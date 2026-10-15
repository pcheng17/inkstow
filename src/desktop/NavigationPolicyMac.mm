#import "NavigationPolicy.h"

#import <WebKit/WebKit.h>

#include <stdexcept>

using inkstow::webview_shell::NavigationGate;

@interface InkstowNavigationDelegate : NSObject <WKNavigationDelegate> {
    NavigationGate* mGate;
}
- (instancetype)initWithGate:(NavigationGate*)gate;
@end

@implementation InkstowNavigationDelegate
- (instancetype)initWithGate:(NavigationGate*)gate
{
    self = [super init];
    if (self) {
        mGate = gate;
    }
    return self;
}

- (void)webView:(WKWebView*)webView
    decidePolicyForNavigationAction:(WKNavigationAction*)action
                    decisionHandler:(void (^)(WKNavigationActionPolicy))decisionHandler
{
    (void)webView;
    const char* uri = action.request.URL.absoluteString.UTF8String;
    const bool allow = action.targetFrame.isMainFrame && uri && mGate->allowInitialDocument(uri);
    decisionHandler(allow ? WKNavigationActionPolicyAllow : WKNavigationActionPolicyCancel);
}
@end

namespace inkstow::webview_shell
{
NavigationPolicy::NavigationPolicy(void* browser)
    : mBrowser(browser)
{
    if (!mBrowser) {
        throw std::runtime_error("Webview browser handle is unavailable");
    }
    auto* delegate = [[InkstowNavigationDelegate alloc] initWithGate:&mGate];
    if (!delegate) {
        throw std::runtime_error("Could not install WKWebView navigation policy");
    }
    mDelegate = delegate;
    [(WKWebView*)mBrowser setNavigationDelegate:delegate];
}

NavigationPolicy::~NavigationPolicy()
{
    [(WKWebView*)mBrowser setNavigationDelegate:nil];
    [(InkstowNavigationDelegate*)mDelegate release];
}
} // namespace inkstow::webview_shell
