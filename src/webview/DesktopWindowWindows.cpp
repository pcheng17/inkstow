#include "DesktopWindow.h"

#include <windows.h>

namespace inkstow::webview_shell
{
namespace
{
constexpr UINT_PTR kOpen = 1001;
constexpr UINT_PTR kSave = 1002;
constexpr UINT_PTR kQuit = 1003;
constexpr UINT_PTR kUndo = 1004;
constexpr UINT_PTR kFollow = 1005;

LRESULT CALLBACK windowProc(HWND handle, UINT message, WPARAM wparam, LPARAM lparam)
{
    auto* shell = static_cast<DesktopWindow*>(GetPropW(handle, L"InkstowDesktopWindow"));
    if (shell) {
        if (message == WM_CLOSE) {
            shell->invoke("inkstowAskQuit()");
            return 0;
        }
        if (message == WM_COMMAND) {
            switch (LOWORD(wparam)) {
            case kOpen:
                shell->invoke("inkstowOpenPicker()");
                return 0;
            case kSave:
                shell->invoke("inkstowSave()");
                return 0;
            case kQuit:
                shell->invoke("inkstowAskQuit()");
                return 0;
            case kUndo:
                shell->invoke("inkstowUndo()");
                return 0;
            case kFollow:
                shell->invoke("inkstowFollow()");
                return 0;
            default:
                break;
            }
        }
        return CallWindowProcW(
            reinterpret_cast<WNDPROC>(shell->previous()),
            handle,
            message,
            wparam,
            lparam);
    }
    return DefWindowProcW(handle, message, wparam, lparam);
}
} // namespace

DesktopWindow::DesktopWindow(webview::webview& window)
    : mWindow(window)
{
    mHandle = window.window().value();
    auto* native = static_cast<HWND>(mHandle);
    if (!SetPropW(native, L"InkstowDesktopWindow", this))
        throw std::runtime_error("Could not attach close guard");
    mPrevious = reinterpret_cast<void*>(
        SetWindowLongPtrW(native, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&windowProc)));
    HMENU bar = CreateMenu();
    HMENU file = CreatePopupMenu();
    HMENU note = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, kOpen, L"Open vault…\tCtrl+O");
    AppendMenuW(file, MF_STRING, kSave, L"Save\tCtrl+S");
    AppendMenuW(file, MF_STRING, kQuit, L"Quit\tCtrl+Q");
    AppendMenuW(note, MF_STRING, kUndo, L"Undo\tCtrl+Z");
    AppendMenuW(note, MF_STRING, kFollow, L"Follow link\tAlt+Enter");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"File");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(note), L"Note");
    SetMenu(native, bar);
}

DesktopWindow::~DesktopWindow()
{
    if (mHandle && IsWindow(static_cast<HWND>(mHandle))) {
        SetWindowLongPtrW(
            static_cast<HWND>(mHandle),
            GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(mPrevious));
        RemovePropW(static_cast<HWND>(mHandle), L"InkstowDesktopWindow");
        HMENU menu = GetMenu(static_cast<HWND>(mHandle));
        SetMenu(static_cast<HWND>(mHandle), nullptr);
        if (menu) DestroyMenu(menu);
    }
}
void* DesktopWindow::previous() const
{
    return mPrevious;
}
void DesktopWindow::invoke(const char* script)
{
    mWindow.eval(script);
}
} // namespace inkstow::webview_shell
