#include <doctest/doctest.h>

#include "NavigationPolicy.h"
#include "WebviewHeader.h"

#include <webkit2/webkit2.h>

#include <chrono>
#include <string>
#include <thread>

namespace
{
void pumpFor(std::chrono::milliseconds duration)
{
    const auto end = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < end) {
        while (g_main_context_pending(nullptr)) {
            g_main_context_iteration(nullptr, FALSE);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
} // namespace

TEST_CASE("NavigationPolicy keeps the embedded Linux page bound and blocks subframes")
{
    int trusted = 0;
    int untrusted = 0;
    webview::webview window(false, nullptr);
    inkstow::webview_shell::NavigationPolicy policy(window.browser_controller().value());
    window.bind("probe", [&](const std::string& args) {
        if (args.find("trusted") != std::string::npos) {
            ++trusted;
        } else {
            ++untrusted;
        }
        return std::string("null");
    });
    window.set_html(
        "<script>window.probe('trusted');"
        "window.addEventListener('message', () => window.probe('evil'))"
        "</script><h1>trusted</h1>");
    pumpFor(std::chrono::milliseconds(2000));
    REQUIRE(trusted == 1);
    auto* view = WEBKIT_WEB_VIEW(window.browser_controller().value());
    window.eval(
        "const frame = document.createElement('iframe');"
        "frame.src = 'data:text/html,<script>parent.postMessage(1, \"*\")<\\/script>';"
        "document.body.append(frame);");
    pumpFor(std::chrono::milliseconds(1000));
    REQUIRE(untrusted == 0);
    window.navigate("data:text/html,<script>window.probe('evil')</script>");
    pumpFor(std::chrono::milliseconds(1000));
    window.navigate("about:blank");
    pumpFor(std::chrono::milliseconds(1000));
    window.eval("if (document.querySelector('h1')) window.probe('trusted')");
    pumpFor(std::chrono::milliseconds(100));
    REQUIRE((trusted == 2 && untrusted == 0));
    const char* uri = webkit_web_view_get_uri(view);
    REQUIRE(uri != nullptr);
    REQUIRE(std::string(uri) == "about:blank");
}
