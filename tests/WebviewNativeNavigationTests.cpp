#include "NavigationPolicy.h"

// Upstream 0.12.0 shadows parameters in its implementation header.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#include <webview/webview.h>
#pragma GCC diagnostic pop

#include <webkit2/webkit2.h>

#include <chrono>
#include <stdexcept>
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

int main() // NOLINT(bugprone-exception-escape)
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
    window.set_html("<script>window.probe('trusted')</script><h1>trusted</h1>");
    pumpFor(std::chrono::milliseconds(2000));
    if (trusted != 1) {
        throw std::runtime_error("Embedded page did not receive its binding");
    }
    window.navigate("data:text/html,<script>window.probe('evil')</script>");
    pumpFor(std::chrono::milliseconds(1000));
    window.navigate("about:blank");
    pumpFor(std::chrono::milliseconds(1000));
    window.eval("if (document.querySelector('h1')) window.probe('trusted')");
    pumpFor(std::chrono::milliseconds(100));
    if (trusted != 2 || untrusted != 0) {
        throw std::runtime_error("Navigation replaced the embedded page or reached its binding");
    }
    auto* view = WEBKIT_WEB_VIEW(window.browser_controller().value());
    const char* uri = webkit_web_view_get_uri(view);
    if (!uri || std::string(uri) != "about:blank") {
        throw std::runtime_error("Embedded page navigated away");
    }
}
