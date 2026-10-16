#include "NavigationPolicy.h"

#include <doctest/doctest.h>

TEST_CASE("NavigationGate permits only the initial embedded document")
{
    using inkstow::webview_shell::NavigationGate;
    NavigationGate gate;
    CHECK_FALSE(gate.allowInitialDocument("https://example.org/"));
    CHECK_FALSE(gate.allowInitialDocument("file:///tmp/page.html"));
    CHECK_FALSE(gate.allowInitialDocument("data:text/html,evil"));
    CHECK(gate.allowInitialDocument("about:blank"));
    CHECK_FALSE(gate.allowInitialDocument("about:blank"));
    CHECK_FALSE(gate.allowInitialDocument("https://example.org/"));

    NavigationGate webView2Gate;
    CHECK_FALSE(webView2Gate.allowInitialWebView2Document(L"data:text/html,evil"));
    CHECK_FALSE(webView2Gate.allowInitialWebView2Document(L"data:text/html;charset=utf-8;base64,"));
    CHECK(webView2Gate.allowInitialWebView2Document(
        L"data:text/html;charset=utf-8;base64,PGgxPk9LPC9oMT4="));
    CHECK_FALSE(webView2Gate.allowInitialDocument("about:blank"));
    CHECK_FALSE(webView2Gate.allowInitialWebView2Document(
        L"data:text/html;charset=utf-8;base64,PGgxPk5ldzwvaDE+"));
}
