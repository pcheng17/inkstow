#include "NavigationPolicy.h"

#include <stdexcept>
#include <string>

namespace
{
void check(bool passed, const char* message)
{
    if (!passed) {
        throw std::runtime_error(message);
    }
}
} // namespace

int main() // NOLINT(bugprone-exception-escape)
{
    using inkstow::webview_shell::NavigationGate;
    NavigationGate gate;
    check(!gate.allowInitialDocument("https://example.org/"), "Remote initial navigation allowed");
    check(!gate.allowInitialDocument("file:///tmp/page.html"), "File navigation allowed");
    check(!gate.allowInitialDocument("data:text/html,evil"), "Data navigation allowed");
    check(gate.allowInitialDocument("about:blank"), "Embedded initial page blocked");
    check(!gate.allowInitialDocument("about:blank"), "Second blank navigation allowed");
    check(!gate.allowInitialDocument("https://example.org/"), "Remote navigation allowed");

    NavigationGate webView2Gate;
    check(
        !webView2Gate.allowInitialWebView2Document(L"data:text/html,evil"),
        "Unencoded WebView2 document allowed");
    check(
        !webView2Gate.allowInitialWebView2Document(L"data:text/html;charset=utf-8;base64,"),
        "Empty WebView2 document allowed");
    check(
        webView2Gate.allowInitialWebView2Document(
            L"data:text/html;charset=utf-8;base64,PGgxPk9LPC9oMT4="),
        "Embedded WebView2 document blocked");
    check(!webView2Gate.allowInitialDocument("about:blank"), "Second WebView2 navigation allowed");
    check(
        !webView2Gate.allowInitialWebView2Document(
            L"data:text/html;charset=utf-8;base64,PGgxPk5ldzwvaDE+"),
        "Second WebView2 document allowed");
}
