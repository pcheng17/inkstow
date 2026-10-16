#include "DesktopWindow.h"

#include <windows.h>

#include <iterator>
#include <stdexcept>

namespace inkstow::webview_shell
{
namespace
{
constexpr UINT_PTR kOpen = 1001;
constexpr UINT_PTR kSave = 1002;
constexpr UINT_PTR kQuit = 1003;
constexpr UINT_PTR kUndo = 1004;

LRESULT CALLBACK acceleratorHook(int code, WPARAM wparam, LPARAM lparam)
{
    if (code == HC_ACTION && wparam == PM_REMOVE) {
        auto* message = reinterpret_cast<MSG*>(lparam);
        if (message->hwnd && (message->message == WM_KEYDOWN || message->message == WM_SYSKEYDOWN))
        {
            auto* root = GetAncestor(message->hwnd, GA_ROOT);
            auto* shell = static_cast<DesktopWindow*>(GetPropW(root, L"InkstowDesktopWindow"));
            if (shell && shell->translate(message)) message->message = WM_NULL;
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

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
    if (!mPrevious) {
        RemovePropW(native, L"InkstowDesktopWindow");
        throw std::runtime_error("Could not guard native window close");
    }
    HMENU bar = CreateMenu();
    HMENU file = CreatePopupMenu();
    HMENU edit = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, kOpen, L"Open vault…\tCtrl+O");
    AppendMenuW(file, MF_STRING, kSave, L"Save\tCtrl+S");
    AppendMenuW(file, MF_STRING, kQuit, L"Quit\tCtrl+Q");
    AppendMenuW(edit, MF_STRING, kUndo, L"Undo\tCtrl+Z");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"File");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(edit), L"Edit");
    SetMenu(native, bar);
    ACCEL shortcuts[] = {
        {FVIRTKEY | FCONTROL, 'O', kOpen},
        {FVIRTKEY | FCONTROL, 'S', kSave},
        {FVIRTKEY | FCONTROL, 'Q', kQuit},
        {FVIRTKEY | FCONTROL, 'Z', kUndo},
    };
    mAccelerators = CreateAcceleratorTableW(shortcuts, static_cast<int>(std::size(shortcuts)));
    if (mAccelerators)
        mKeyboardHook =
            SetWindowsHookExW(WH_GETMESSAGE, acceleratorHook, nullptr, GetCurrentThreadId());
    if (!mKeyboardHook) {
        if (mAccelerators) DestroyAcceleratorTable(static_cast<HACCEL>(mAccelerators));
        SetMenu(native, nullptr);
        DestroyMenu(bar);
        SetWindowLongPtrW(native, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(mPrevious));
        RemovePropW(native, L"InkstowDesktopWindow");
        throw std::runtime_error("Could not register native menu shortcuts");
    }
}

DesktopWindow::~DesktopWindow()
{
    if (mKeyboardHook) UnhookWindowsHookEx(static_cast<HHOOK>(mKeyboardHook));
    if (mAccelerators) DestroyAcceleratorTable(static_cast<HACCEL>(mAccelerators));
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
bool DesktopWindow::translate(void* message) const
{
    return TranslateAcceleratorW(
               static_cast<HWND>(mHandle),
               static_cast<HACCEL>(mAccelerators),
               static_cast<MSG*>(message)) != 0;
}
void DesktopWindow::invoke(const char* script)
{
    mWindow.eval(script);
}
} // namespace inkstow::webview_shell
