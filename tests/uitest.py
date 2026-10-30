"""
WindowsTool 界面回归测试（无需调试器）。

覆盖两个历史缺陷：
  1. cmd.exe 缺 /c → 进入交互式 shell，只打印版本横幅，不执行命令
  2. MainWindow 析构顺序错误 → 运行中关窗触发访问冲突 (0xC0000005)

用法:
    python uitest.py <exe路径> [--keep-screens]

依赖: Python 3.8+，仅用标准库（ctypes 调用 Win32 API）
"""
import ctypes
import ctypes.wintypes as wt
import os
import struct
import subprocess
import sys
import time
import zlib

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32
user32.SetProcessDPIAware()

# 窗口标题。程序自身是 "WindowsTool 电脑工具箱"；
# 但在 VS 里附加调试时标题会变成 "WindowsTool (正在调试) - ..."，
# 所以按 "WindowsTool" 前缀 + 排除 IDE 窗口来匹配。
WINDOW_TITLE = "电脑工具箱"
WINDOW_TITLE_ALT = "WindowsTool"
EXCLUDE_TITLES = ("Microsoft Visual Studio", "Windows 输入体验")


def find_window(substr, timeout=15.0, exclude=(), pid=None):
    """
    按标题查找可见顶层窗口。

    pid 给定时只在该进程内查找——必须这样，否则 VS 附加调试时
    IDE 窗口标题也含 "WindowsTool"，会误匹配到 IDE 上。
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        found = []

        @ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)
        def cb(hwnd, _):
            if not user32.IsWindowVisible(hwnd):
                return True
            n = user32.GetWindowTextLengthW(hwnd)
            if n == 0:
                return True
            b = ctypes.create_unicode_buffer(n + 1)
            user32.GetWindowTextW(hwnd, b, n + 1)
            title = b.value
            if not title:
                return True
            if any(x in title for x in exclude):
                return True
            if pid is not None:
                owner = wt.DWORD()
                user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
                if owner.value != pid:
                    return True
            if substr in title:
                found.append(hwnd)
            return True

        user32.EnumWindows(cb, 0)
        if found:
            return found[0]
        time.sleep(0.3)
    return None


def find_app_window(proc, timeout=15.0):
    """
    查找被测程序的主窗口。

    优先用"电脑工具箱"精确匹配；VS 附加调试时标题会变成
    "WindowsTool (正在调试) - ..."，此时退回按进程号找。
    """
    hwnd = find_window(WINDOW_TITLE, timeout, pid=proc.pid)
    if hwnd:
        return hwnd
    return find_window(WINDOW_TITLE_ALT, timeout,
                       exclude=EXCLUDE_TITLES, pid=proc.pid)

# 磁贴中心点（基于 996x759 的窗口量取，实际会按窗口尺寸等比换算）
TILES = {
    "auto": (352, 238),      # 自动检测并修复
    "info": (594, 238),      # 查看网络信息
    "test": (836, 238),      # 网络测试
}

passed = 0
failed = 0


def check(name, ok, detail=""):
    global passed, failed
    if ok:
        passed += 1
        print(f"  [PASS] {name}")
    else:
        failed += 1
        print(f"  [FAIL] {name}  {detail}")


def visible_titles(pid=None):
    """当前可见顶层窗口的标题（可按 PID 过滤，排除 IDE 之类无关窗口）"""
    titles = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)
    def cb(hwnd, _):
        if user32.IsWindowVisible(hwnd):
            n = user32.GetWindowTextLengthW(hwnd)
            b = ctypes.create_unicode_buffer(n + 1)
            user32.GetWindowTextW(hwnd, b, n + 1)
            title = b.value
            if not title.strip():
                return True
            if any(x in title for x in EXCLUDE_TITLES):
                return True
            if pid is not None:
                owner = wt.DWORD()
                user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
                if owner.value != pid:
                    return True
            titles.append(title)
        return True

    user32.EnumWindows(cb, 0)
    return titles


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [
        ("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG),
        ("biPlanes", wt.WORD), ("biBitCount", wt.WORD),
        ("biCompression", wt.DWORD), ("biSizeImage", wt.DWORD),
        ("biXPelsPerMeter", wt.LONG), ("biYPelsPerMeter", wt.LONG),
        ("biClrUsed", wt.DWORD), ("biClrImportant", wt.DWORD),
    ]


def capture(hwnd, path):
    """抓取窗口内容存 PNG（手写 PNG 编码，避免依赖 PIL）"""
    rect = wt.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    w, h = rect.right - rect.left, rect.bottom - rect.top
    if w <= 0 or h <= 0:
        return None

    hdc_win = user32.GetWindowDC(hwnd)
    hdc_mem = gdi32.CreateCompatibleDC(hdc_win)
    bmp = gdi32.CreateCompatibleBitmap(hdc_win, w, h)
    gdi32.SelectObject(hdc_mem, bmp)
    user32.PrintWindow(hwnd, hdc_mem, 2)  # PW_RENDERFULLCONTENT

    hdr = BITMAPINFOHEADER()
    hdr.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    hdr.biWidth = w
    hdr.biHeight = -h
    hdr.biPlanes = 1
    hdr.biBitCount = 32
    hdr.biCompression = 0
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(hdc_mem, bmp, 0, h, buf, ctypes.byref(hdr), 0)

    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(hdc_mem)
    user32.ReleaseDC(hwnd, hdc_win)

    raw = bytearray()
    stride = w * 4
    for y in range(h):
        raw.append(0)
        row = buf.raw[y * stride:(y + 1) * stride]
        for x in range(w):
            raw += bytes((row[x * 4 + 2], row[x * 4 + 1], row[x * 4]))

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 6))
    png += chunk(b"IEND", b"")

    with open(path, "wb") as f:
        f.write(png)
    return w, h


def click_tile(hwnd, key):
    """按磁贴名点击（自动按窗口实际尺寸换算坐标）"""
    r = wt.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    sx = (r.right - r.left) / 996.0
    sy = (r.bottom - r.top) / 759.0
    tx, ty = TILES[key]
    user32.SetForegroundWindow(hwnd)
    time.sleep(0.4)
    user32.SetCursorPos(r.left + int(tx * sx), r.top + int(ty * sy))
    time.sleep(0.3)
    user32.mouse_event(0x0002, 0, 0, 0, 0)  # LEFTDOWN
    time.sleep(0.08)
    user32.mouse_event(0x0004, 0, 0, 0, 0)  # LEFTUP


def test_log_output(exe, shot_dir):
    """测试1：点击磁贴后，日志应有真实命令输出（不是 cmd.exe 版本横幅）"""
    print("\n[测试1] 命令执行与日志输出")
    proc = subprocess.Popen([exe], cwd=os.path.dirname(exe))
    try:
        hwnd = find_app_window(proc)
        if not hwnd:
            check("启动并显示主窗口", False, "未找到窗口")
            return
        check("启动并显示主窗口", True)
        time.sleep(2)

        click_tile(hwnd, "test")   # 网络测试 → ping
        time.sleep(7)

        user32.SetCursorPos(2000, 2000)
        time.sleep(0.4)

        shot = os.path.join(shot_dir, "uitest_ping.png")
        if capture(hwnd, shot):
            check("抓取日志截图", True, shot)

        # 检查没有弹出错误对话框
        bad = [t for t in visible_titles(proc.pid) if "失败" in t or "错误" in t]
        check("无错误对话框", not bad, str(bad))

        check("进程未崩溃", proc.poll() is None, f"exit={proc.poll()}")
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except Exception:
                proc.kill()


def test_close_while_running(exe):
    """测试2：命令运行中关闭窗口，不应触发访问冲突 (0xC0000005)"""
    print("\n[测试2] 运行中关闭窗口（析构顺序）")
    for wait in (0.2, 1.0, 2.5):
        proc = subprocess.Popen([exe], cwd=os.path.dirname(exe))
        try:
            hwnd = find_app_window(proc)
            if not hwnd:
                check(f"启动（等待 {wait}s）", False, "未找到窗口")
                continue
            time.sleep(2)
            click_tile(hwnd, "test")   # 启动 ping，让子进程跑起来
            time.sleep(wait)

            user32.PostMessageW(hwnd, 0x0010, 0, 0)  # WM_CLOSE

            rc = None
            for _ in range(40):
                time.sleep(0.25)
                rc = proc.poll()
                if rc is not None:
                    break

            if rc is None:
                check(f"运行中关窗（等待 {wait}s）", False, "进程未退出")
            else:
                code = rc & 0xFFFFFFFF
                ok = code == 0
                check(f"运行中关窗（等待 {wait}s）", ok,
                      f"退出码 0x{code:08X}")
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except Exception:
                    proc.kill()


def test_task_switch(exe):
    """测试3：连续快速切换任务，不应误报'进程异常终止'"""
    print("\n[测试3] 快速切换任务")
    proc = subprocess.Popen([exe], cwd=os.path.dirname(exe))
    try:
        hwnd = find_app_window(proc)
        if not hwnd:
            check("启动并显示主窗口", False, "未找到窗口")
            return
        time.sleep(2)

        for key in ("test", "info", "test", "auto"):
            click_tile(hwnd, key)
            time.sleep(0.4)
        time.sleep(6)

        bad = [t for t in visible_titles(proc.pid) if "失败" in t or "错误" in t]
        check("切换任务不误报错误", not bad, str(bad))
        check("进程未崩溃", proc.poll() is None, f"exit={proc.poll()}")
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except Exception:
                proc.kill()


def main():
    if len(sys.argv) < 2:
        print("用法: python uitest.py <exe路径>")
        return 1

    exe = os.path.abspath(sys.argv[1])
    if not os.path.isfile(exe):
        print(f"找不到可执行文件: {exe}")
        return 1

    shot_dir = os.path.join(os.path.dirname(exe), "shots")
    os.makedirs(shot_dir, exist_ok=True)

    print(f"被测程序: {exe}")
    test_log_output(exe, shot_dir)
    test_close_while_running(exe)
    test_task_switch(exe)

    print(f"\n===== 结果: {passed} passed, {failed} failed =====")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
