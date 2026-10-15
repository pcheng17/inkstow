#include "Bridge.h"
#include "DesktopServices.h"
#include "DesktopWindow.h"
#include "LocalHtml.h"
#include "NavigationPolicy.h"
#include "VaultWatch.h"
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
        inkstow::webview_shell::VaultWatch watcher([&window] {
            window.dispatch([&window] {
                window.eval("if (typeof inkstowRefresh === 'function') inkstowRefresh()");
            });
        });
        services.setWatcher(&watcher);
        inkstow::webview_shell::NavigationPolicy navigation(window.browser_controller().value());
        window.bind("inkstowRequest", [&bridge](const std::string& args) {
            return bridge.handle(args);
        });
        window.bind("inkstowExit", [&window](const std::string&) {
            window.terminate();
            return std::string("null");
        });
        inkstow::webview_shell::DesktopWindow desktop(window);
        window.set_html(kLocalHtml);
        window.run();
        services.setWatcher(nullptr);
    } catch (const std::exception& error) {
        std::cerr << "Could not start Inkstow webview: " << error.what() << '\n';
        return 1;
    }
}
