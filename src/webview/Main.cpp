#include "Bridge.h"
#include "DesktopServices.h"
#include "LocalHtml.h"
#include "NavigationPolicy.h"
#include "WebviewHeader.h"

#include <exception>
#include <iostream>
#include <string>

int main()
{
    try {
        inkstow::webview_shell::DesktopServices services;
        inkstow::webview_shell::Bridge bridge(&services);
        webview::webview window(false, nullptr);
        window.set_title("Inkstow");
        window.set_size(960, 720, WEBVIEW_HINT_NONE);
        inkstow::webview_shell::NavigationPolicy navigation(window.browser_controller().value());
        window.bind("inkstowRequest", [&bridge](const std::string& args) {
            return bridge.handle(args);
        });
        window.set_html(kLocalHtml);
        window.run();
    } catch (const std::exception& error) {
        std::cerr << "Could not start Inkstow webview: " << error.what() << '\n';
        return 1;
    }
}
