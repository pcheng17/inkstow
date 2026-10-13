"""Send WM_DELETE_WINDOW to an X11 window, as a window manager's close button does.

Usage: python3 -I wm-close.py WINDOW_ID  (uses $DISPLAY)
"""
import ctypes
import ctypes.util
import sys


class ClientMessage(ctypes.Structure):
    _fields_ = [
        ("type", ctypes.c_int),
        ("serial", ctypes.c_ulong),
        ("send_event", ctypes.c_int),
        ("display", ctypes.c_void_p),
        ("window", ctypes.c_ulong),
        ("message_type", ctypes.c_ulong),
        ("format", ctypes.c_int),
        ("data", ctypes.c_long * 5),
    ]


class XEvent(ctypes.Union):
    _fields_ = [("xclient", ClientMessage), ("pad", ctypes.c_long * 24)]


x11 = ctypes.CDLL(ctypes.util.find_library("X11"))
x11.XOpenDisplay.restype = ctypes.c_void_p
x11.XInternAtom.restype = ctypes.c_ulong
x11.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
x11.XSendEvent.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_long, ctypes.c_void_p]
x11.XFlush.argtypes = [ctypes.c_void_p]
x11.XCloseDisplay.argtypes = [ctypes.c_void_p]

display = x11.XOpenDisplay(None)
if not display:
    sys.exit("cannot open display")
window = int(sys.argv[1], 0)
event = XEvent()
event.xclient.type = 33  # ClientMessage
event.xclient.window = window
event.xclient.message_type = x11.XInternAtom(display, b"WM_PROTOCOLS", 0)
event.xclient.format = 32
event.xclient.data[0] = x11.XInternAtom(display, b"WM_DELETE_WINDOW", 0)
event.xclient.data[1] = 0  # CurrentTime
x11.XSendEvent(display, window, 0, 0, ctypes.byref(event))
x11.XFlush(display)
x11.XCloseDisplay(display)
