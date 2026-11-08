"""Exercise the shipped EXE with marked synthetic input, without invoking Alt+Tab.

An older test hook consumes only our marked Tab events that pass the app's newer
hook. This observes real hook propagation and prevents the shell switching apps.
No physical/user key events are captured, stored, or suppressed by this fixture.
Run on an interactive Windows desktop with the tested app initially closed.
"""
from __future__ import annotations
import ctypes as C
from ctypes import wintypes as W
from pathlib import Path
import subprocess
import sys
import time

user = C.WinDLL('user32', use_last_error=True)
kernel = C.WinDLL('kernel32', use_last_error=True)
PTR = C.c_size_t
LRESULT = C.c_ssize_t
CLASS = 'RhythmGuard.AltTab.Window.v1'
MARKER = 0x524754455354
TAB, ALT, CTRL, SHIFT = 9, 0x12, 0x11, 0x10
WM_CLOSE, WM_COMMAND, WM_KEYUP, WM_SYSKEYUP = 0x10, 0x111, 0x101, 0x105

class KeyboardInput(C.Structure):
    _fields_ = [('wVk', W.WORD), ('wScan', W.WORD), ('dwFlags', W.DWORD),
                ('time', W.DWORD), ('dwExtraInfo', PTR)]

class MouseInput(C.Structure):
    _fields_ = [('dx', W.LONG), ('dy', W.LONG), ('mouseData', W.DWORD),
                ('dwFlags', W.DWORD), ('time', W.DWORD), ('dwExtraInfo', PTR)]

class HardwareInput(C.Structure):
    _fields_ = [('uMsg', W.DWORD), ('wParamL', W.WORD), ('wParamH', W.WORD)]

class InputUnion(C.Union):
    _fields_ = [('ki', KeyboardInput), ('mi', MouseInput), ('hi', HardwareInput)]

class Input(C.Structure):
    _anonymous_ = ('value',)
    _fields_ = [('type', W.DWORD), ('value', InputUnion)]

class HookInput(C.Structure):
    _fields_ = [('vkCode', W.DWORD), ('scanCode', W.DWORD), ('flags', W.DWORD),
                ('time', W.DWORD), ('dwExtraInfo', PTR)]

HOOKPROC = C.WINFUNCTYPE(LRESULT, C.c_int, W.WPARAM, W.LPARAM)
user.SetWindowsHookExW.argtypes = [C.c_int, HOOKPROC, W.HINSTANCE, W.DWORD]
user.SetWindowsHookExW.restype = W.HHOOK
user.CallNextHookEx.argtypes = [W.HHOOK, C.c_int, W.WPARAM, W.LPARAM]
user.CallNextHookEx.restype = LRESULT
user.UnhookWindowsHookEx.argtypes = [W.HHOOK]
user.FindWindowW.argtypes = [W.LPCWSTR, W.LPCWSTR]
user.FindWindowW.restype = W.HWND
user.GetDlgItem.argtypes = [W.HWND, C.c_int]
user.GetDlgItem.restype = W.HWND
user.GetWindowTextW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
user.IsWindowEnabled.argtypes = [W.HWND]
user.IsWindowVisible.argtypes = [W.HWND]
user.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
user.SendInput.argtypes = [W.UINT, C.POINTER(Input), C.c_int]
user.SendInput.restype = W.UINT
user.PeekMessageW.argtypes = [C.POINTER(W.MSG), W.HWND, W.UINT, W.UINT, W.UINT]
user.TranslateMessage.argtypes = [C.POINTER(W.MSG)]
user.DispatchMessageW.argtypes = [C.POINTER(W.MSG)]
user.DispatchMessageW.restype = LRESULT
kernel.GetModuleHandleW.argtypes = [W.LPCWSTR]
kernel.GetModuleHandleW.restype = W.HMODULE
user.GetAsyncKeyState.argtypes = [C.c_int]
user.GetAsyncKeyState.restype = W.SHORT

received: list[tuple[int, bool]] = []
checks: list[str] = []
process: subprocess.Popen | None = None

@HOOKPROC
def sink(code, message, data):
    if code == 0:
        key = C.cast(data, C.POINTER(HookInput)).contents
        if key.dwExtraInfo == MARKER and key.vkCode == TAB:
            received.append((key.vkCode, message not in (WM_KEYUP, WM_SYSKEYUP)))
            return 1
    return user.CallNextHookEx(None, code, message, data)

def pump(seconds=0.12):
    deadline = time.monotonic() + seconds
    msg = W.MSG()
    while time.monotonic() < deadline:
        while user.PeekMessageW(C.byref(msg), None, 0, 0, 1):
            user.TranslateMessage(C.byref(msg))
            user.DispatchMessageW(C.byref(msg))
        time.sleep(0.002)

def wait_for(predicate, timeout=4):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        pump(0.02)
    raise AssertionError('Timed out waiting for app state')

def text(hwnd):
    result = C.create_unicode_buffer(256)
    user.GetWindowTextW(hwnd, result, 256)
    return result.value

def expect(condition, description):
    if not condition:
        raise AssertionError(description)
    checks.append(description)
    print('PASS:', description, flush=True)

def send(*events):
    items = (Input * len(events))()
    for item, (key, down) in zip(items, events):
        item.type = 1
        flags = (0 if down else 2) | (1 if key == 0xA5 else 0)
        item.ki = KeyboardInput(key, 0, flags, 0, MARKER)
    count = user.SendInput(len(items), items, C.sizeof(Input))
    if count != len(items):
        raise OSError(C.get_last_error(), 'SendInput did not deliver the test events')
    pump()

def command(hwnd, id_):
    user.PostMessageW(hwnd, WM_COMMAND, id_, 0)
    pump()

def check_sequence(events, expected, description):
    received.clear()
    send(*events)
    actual = [down for _, down in received]
    expect(actual == expected, f'{description} (received={actual})')

def launch(executable):
    global process
    process = subprocess.Popen([str(executable)])
    hwnd = wait_for(lambda: user.FindWindowW(CLASS, None))
    toggle = wait_for(lambda: user.GetDlgItem(hwnd, 101))
    try:
        wait_for(lambda: user.IsWindowEnabled(toggle) and text(toggle) == '차단 끄기')
    except AssertionError as error:
        raise AssertionError(f'App not ready: title={text(hwnd)!r}, '
                             f'button={text(toggle)!r}, exit={process.poll()}') from error
    return hwnd, toggle

def close(hwnd):
    user.PostMessageW(hwnd, WM_CLOSE, 0, 0)
    wait_for(lambda: process.poll() is not None)
    expect(process.returncode == 0, 'clean exit code')

def main():
    global process
    executable = Path(sys.argv[1]).resolve()
    if user.FindWindowW(CLASS, None):
        raise RuntimeError('Close the existing Rhythm Guard before running this fixture')
    # Do not disturb a physical key that the user happens to be holding.
    if any(user.GetAsyncKeyState(k) & 0x8000 for k in (TAB, ALT, CTRL, SHIFT)):
        raise RuntimeError('Release Alt/Tab/Ctrl/Shift before running this fixture')
    hook = user.SetWindowsHookExW(13, sink, kernel.GetModuleHandleW(None), 0)
    if not hook:
        raise C.WinError(C.get_last_error())
    hwnd = None
    try:
        hwnd, toggle = launch(executable)
        expect(text(toggle) == '차단 끄기', 'launch immediately enables blocking')
        check_sequence([(ALT, True), (TAB, True), (TAB, False), (ALT, False)],
                       [], 'enabled Alt+Tab down and up are suppressed')
        check_sequence([(ALT, True), (SHIFT, True), (TAB, True), (TAB, False),
                        (SHIFT, False), (ALT, False)],
                       [], 'Alt+Shift+Tab is suppressed')
        check_sequence([(CTRL, True), (ALT, True), (TAB, True), (TAB, False),
                        (ALT, False), (CTRL, False)],
                       [], 'Ctrl+Alt+Tab is suppressed')
        check_sequence([(0xA5, True), (TAB, True), (TAB, False), (0xA5, False)],
                       [], 'right Alt+Tab is suppressed')
        check_sequence([(TAB, True), (TAB, False)],
                       [True, False], 'ordinary Tab passes while blocking is on')
        check_sequence([(SHIFT, True), (TAB, True), (TAB, False), (SHIFT, False)],
                       [True, False], 'ordinary Shift+Tab passes')
        check_sequence([(ALT, True), (TAB, True), (ALT, False), (TAB, True), (TAB, False)],
                       [], 'release Alt first: Tab repeat and release cannot leak')
        check_sequence([(TAB, True), (ALT, True), (TAB, True), (TAB, False), (ALT, False)],
                       [True, False], 'Tab before Alt: block repeat but preserve matching release')
        command(hwnd, 101)
        wait_for(lambda: text(toggle) == '차단 켜기')
        expect(True, 'window button disables blocking')
        check_sequence([(ALT, True), (TAB, True), (TAB, False), (ALT, False)],
                       [True, False], 'window button restores Alt+Tab propagation')
        command(hwnd, 101)
        wait_for(lambda: text(toggle) == '차단 끄기')
        expect(True, 'window button re-enables blocking')
        command(hwnd, 102)
        wait_for(lambda: not user.IsWindowVisible(hwnd))
        expect(True, 'explicit tray button hides the window')
        command(hwnd, 105)
        wait_for(lambda: text(toggle) == '차단 켜기')
        expect(True, 'tray menu release works while hidden')
        duplicate = subprocess.Popen([str(executable)])
        wait_for(lambda: duplicate.poll() is not None)
        expect(duplicate.returncode == 0 and process.poll() is None,
               'second launch reuses the original instance')
        wait_for(lambda: user.IsWindowVisible(hwnd))
        command(hwnd, 101)
        wait_for(lambda: text(toggle) == '차단 끄기')
        close(hwnd)
        hwnd = None
        check_sequence([(ALT, True), (TAB, True), (TAB, False), (ALT, False)],
                       [True, False], 'closing while enabled restores Alt+Tab propagation')
        hwnd, toggle = launch(executable)
        expect(text(toggle) == '차단 끄기', 'relaunch enables blocking without a shortcut')
        command(hwnd, 105)
        wait_for(lambda: text(toggle) == '차단 켜기')
        close(hwnd)
        hwnd = None
        print(f'PASS: {len(checks)} Windows integration checks', flush=True)
    finally:
        # Always release only the fixture's modifiers and close only its child.
        try:
            send((TAB, False), (ALT, False), (0xA5, False), (CTRL, False), (SHIFT, False))
        except OSError as error:
            print(f'Cleanup input unavailable: {error}', flush=True)
        if process and process.poll() is None:
            owned = user.FindWindowW(CLASS, None)
            if owned:
                user.PostMessageW(owned, WM_CLOSE, 0, 0)
                pump(0.4)
            if process.poll() is None:
                process.terminate()
                process.wait(3)
        user.UnhookWindowsHookEx(hook)

if __name__ == '__main__':
    main()
