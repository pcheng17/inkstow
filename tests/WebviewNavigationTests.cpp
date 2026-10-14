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
}
