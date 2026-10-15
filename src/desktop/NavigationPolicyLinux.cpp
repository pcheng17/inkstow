#include "NavigationPolicy.h"

#include <webkit2/webkit2.h>

#include <stdexcept>

namespace inkstow::webview_shell
{
NavigationPolicy::NavigationPolicy(void* browser)
    : mBrowser(browser)
{
    if (!mBrowser) {
        throw std::runtime_error("Webview browser handle is unavailable");
    }
    mSignal = g_signal_connect(
        mBrowser,
        "decide-policy",
        G_CALLBACK(
            +[](WebKitWebView*,
                WebKitPolicyDecision* decision,
                WebKitPolicyDecisionType type,
                gpointer context) -> gboolean {
                if (type == WEBKIT_POLICY_DECISION_TYPE_RESPONSE) {
                    return FALSE;
                }
                bool allow = false;
                if (type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) {
                    auto* action = webkit_navigation_policy_decision_get_navigation_action(
                        WEBKIT_NAVIGATION_POLICY_DECISION(decision));
                    auto* request = webkit_navigation_action_get_request(action);
                    const char* uri = webkit_uri_request_get_uri(request);
                    allow =
                        uri &&
                        static_cast<NavigationPolicy*>(context)->mGate.allowInitialDocument(uri);
                }
                if (allow) {
                    webkit_policy_decision_use(decision);
                } else {
                    webkit_policy_decision_ignore(decision);
                }
                return TRUE;
            }),
        this);
    if (!mSignal) {
        throw std::runtime_error("Could not install WebKit navigation policy");
    }
}

NavigationPolicy::~NavigationPolicy()
{
    g_signal_handler_disconnect(mBrowser, mSignal);
}
} // namespace inkstow::webview_shell
