#pragma once

// Upstream webview 0.12.0 shadows parameters in its header. Keep this policy
// in one place for the shell and its native-window tests.
#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include <webview/webview.h>
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
