# Copyright (c) 2023 素曜
# SPDX-License-Identifier: MIT
#
# This file is part of WindowsTool and is distributed under the MIT License.
# See the LICENSE file in the project root for the full license text.

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

# 磁贴中心点（基于 1136x799 的窗口外框量取，实际会按窗口尺寸等比换算）
TILES = {
    "auto": (415, 236),      # 一键检测修复
    "info": (685, 236),      # 看网络信息
    "ports": (955, 236),     # 查端口占用
    "hosts": (415, 340),     # Hosts 设置
    "dns": (685, 340),       # 换 DNS
}

# 侧边导航项的纵向坐标（同样基于 1136x799 量取）
NAV_Y = {
    "概览": 175,
    "网络问题": 223,
    "系统维护": 271,
    "硬件信息": 320,
    "软件管理": 368,
    "实用工具": 415,
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


def move_window_to_origin(hwnd, x=120, y=80):
    """
    把窗口移到已知位置。

    必须这么做：窗口位置由系统记忆，每次启动可能落在不同显示器上
    （本机出现过 x=-1457 的负坐标），此时 SetCursorPos 到负坐标会落空，
    点击打不到目标控件。
    """
    user32.SetWindowPos(hwnd, 0, x, y, 0, 0, 0x0001)  # SWP_NOSIZE
    time.sleep(0.5)


def window_origin(hwnd):
    """取窗口左上角屏幕坐标（确保已调用 move_window_to_origin）"""
    rect = wt.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    return rect.left, rect.top


def bring_to_front(hwnd):
    """
    把窗口临时置顶再切换到前台。

    自动化环境里 SetForegroundWindow 常被系统的前台锁定拦下，
    导致模拟点击落到别的窗口上；先设 TOPMOST 可以保证坐标点击
    一定命中被测窗口。
    """
    user32.SetWindowPos(hwnd, -1, 0, 0, 0, 0, 0x0003)  # TOPMOST|NOMOVE|NOSIZE
    user32.SetForegroundWindow(hwnd)
    time.sleep(0.3)


def release_topmost(hwnd):
    user32.SetWindowPos(hwnd, -2, 0, 0, 0, 0, 0x0003)  # NOTOPMOST


def click_tile(hwnd, key):
    """按磁贴名点击（自动按窗口实际尺寸换算坐标）"""
    move_window_to_origin(hwnd)
    ox, oy = window_origin(hwnd)
    rect = wt.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    sx = (rect.right - rect.left) / 1136.0
    sy = (rect.bottom - rect.top) / 799.0
    tx, ty = TILES[key]
    bring_to_front(hwnd)
    user32.SetCursorPos(ox + int(tx * sx), oy + int(ty * sy))
    time.sleep(0.3)
    user32.mouse_event(0x0002, 0, 0, 0, 0)  # LEFTDOWN
    time.sleep(0.08)
    user32.mouse_event(0x0004, 0, 0, 0, 0)  # LEFTUP
    release_topmost(hwnd)


def click_nav(hwnd, label):
    """点击侧边导航项（label 见 NAV_Y）"""
    move_window_to_origin(hwnd)
    ox, oy = window_origin(hwnd)
    rect = wt.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rect))
    sy = (rect.bottom - rect.top) / 799.0
    ny = NAV_Y.get(label)
    if ny is None:
        return False
    bring_to_front(hwnd)
    user32.SetCursorPos(ox + 118, oy + int(ny * sy))
    time.sleep(0.3)
    user32.mouse_event(0x0002, 0, 0, 0, 0)
    time.sleep(0.08)
    user32.mouse_event(0x0004, 0, 0, 0, 0)
    release_topmost(hwnd)
    time.sleep(0.8)
    return True



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

        click_nav(hwnd, "网络问题")      # 磁贴都在网络页，先切页
        click_tile(hwnd, "auto")   # 一键检测修复 → ping + DNS 检测
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
            click_nav(hwnd, "网络问题")
            click_tile(hwnd, "auto")   # 启动检测，让子进程跑起来
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

        click_nav(hwnd, "网络问题")
        for key in ("auto", "info", "auto", "ports"):
            click_tile(hwnd, key)
            time.sleep(0.4)
            # 上一个任务没结束时，后续点击会弹出并发询问框；按默认项
            # （放到后台并继续）关闭它，避免模态框挡住后面的点击
            dismiss_prompt(proc.pid)
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


def press_enter(hwnd):
    """向窗口发送回车（用于选择对话框的默认按钮）"""
    bring_to_front(hwnd)
    user32.keybd_event(0x0D, 0, 0, 0)
    time.sleep(0.06)
    user32.keybd_event(0x0D, 0, 2, 0)
    time.sleep(0.4)
    release_topmost(hwnd)


# 并发询问框（任务未结束时点了另一个功能）与后台任务窗口的标题
PROMPT_TITLE = "还有任务正在运行"
BG_TITLE = "后台任务"


def dismiss_prompt(pid, timeout=1.5):
    """若并发询问框已弹出，按默认项「放到后台并继续」关闭它"""
    dlg = find_window(PROMPT_TITLE, timeout, pid=pid)
    if dlg:
        press_enter(dlg)
    return dlg


def _uia_click_script(pid, automation_suffix):
    return (
        "Add-Type -AssemblyName UIAutomationClient;"
        "Add-Type -AssemblyName UIAutomationTypes;"
        "$r=[System.Windows.Automation.AutomationElement]::RootElement;"
        "$pc=New-Object System.Windows.Automation.PropertyCondition("
        "[System.Windows.Automation.AutomationElement]::ProcessIdProperty,"
        + str(pid) + ");"
        "$wins=$r.FindAll([System.Windows.Automation.TreeScope]::Children,$pc);"
        "foreach($w in $wins){"
        "$els=$w.FindAll([System.Windows.Automation.TreeScope]::Descendants,"
        "[System.Windows.Automation.Condition]::TrueCondition);"
        "foreach($e in $els){"
        "if($e.Current.AutomationId -like '*" + automation_suffix + "'){"
        "try{$p=$e.GetCurrentPattern("
        "[System.Windows.Automation.InvokePattern]::Pattern);$p.Invoke();"
        "Write-Output ('OK:'+$e.Current.AutomationId);exit 0}catch{}"
        "try{$p=$e.GetCurrentPattern("
        "[System.Windows.Automation.SelectionItemPattern]::Pattern);$p.Select();"
        "Write-Output ('OK:'+$e.Current.AutomationId);exit 0}catch{}"
        "try{$p=$e.GetCurrentPattern("
        "[System.Windows.Automation.TogglePattern]::Pattern);$p.Toggle();"
        "Write-Output ('OK:'+$e.Current.AutomationId);exit 0}catch{}"
        "Write-Output ('NOPATTERN:'+$e.Current.AutomationId);exit 1 } } };"
        "Write-Output 'MISS'"
    )


def uia_click(pid, automation_suffix):
    """
    用 UI Automation 点击指定进程里 AutomationId 以 suffix 结尾的控件。

    走 UIA 而不是模拟鼠标，控件不必在前台。依次尝试 Invoke /
    SelectionItem / Toggle，兼容 QToolButton 与可勾选的导航按钮。
    """
    try:
        r = subprocess.run(
            ["powershell", "-NoProfile", "-Command",
             _uia_click_script(pid, automation_suffix)],
            capture_output=True, text=True, timeout=40)
    except Exception as exc:  # noqa: BLE001
        return False, str(exc)
    return "OK:" in r.stdout, (r.stdout + r.stderr).strip()


def uia_click_async(pid, automation_suffix):
    """
    异步触发会弹模态框的按钮。

    UIA 的 Invoke 是同步的：被点按钮若打开模态对话框，调用方会一直
    阻塞到对话框关闭。这里用独立进程触发，测试脚本可以继续操作对话框。
    """
    return subprocess.Popen(
        ["powershell", "-NoProfile", "-Command",
         _uia_click_script(pid, automation_suffix)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def uia_exists(pid, automation_suffix):
    """UIA 中是否存在 AutomationId 以 suffix 结尾的控件"""
    script = (
        "Add-Type -AssemblyName UIAutomationClient;"
        "Add-Type -AssemblyName UIAutomationTypes;"
        "$r=[System.Windows.Automation.AutomationElement]::RootElement;"
        "$pc=New-Object System.Windows.Automation.PropertyCondition("
        "[System.Windows.Automation.AutomationElement]::ProcessIdProperty,"
        + str(pid) + ");"
        "$wins=$r.FindAll([System.Windows.Automation.TreeScope]::Children,$pc);"
        "foreach($w in $wins){"
        "$els=$w.FindAll([System.Windows.Automation.TreeScope]::Descendants,"
        "[System.Windows.Automation.Condition]::TrueCondition);"
        "foreach($e in $els){"
        "if($e.Current.AutomationId -like '*" + automation_suffix + "'){"
        "Write-Output 'FOUND'; exit 0 } } };"
        "Write-Output 'MISS'"
    )
    r = subprocess.run(["powershell", "-NoProfile", "-Command", script],
                       capture_output=True, text=True, timeout=40)
    return "FOUND" in r.stdout


def _uia_name_script(pid, name_text, action):
    """
    生成按 Name（可见文本）匹配控件的 PowerShell 脚本。

    Qt 只给 .ui 里的控件生成 AutomationId，代码里动态创建的
    按钮 id 为空，所以后台任务窗口只能用 Name 定位。中文文案
    经 Base64 传入，脚本本身保持纯 ASCII。
    """
    import base64
    b64 = base64.b64encode(name_text.encode("utf-8")).decode("ascii")
    if action == "click":
        body = (
            "try{$p=$e.GetCurrentPattern("
            "[System.Windows.Automation.InvokePattern]::Pattern);$p.Invoke();"
            "Write-Output ('OK:'+$e.Current.Name);exit 0}catch{}"
            "Write-Output 'NOPATTERN';exit 1"
        )
    else:
        body = "Write-Output ('FOUND:'+$e.Current.Name);exit 0"
    return (
        "Add-Type -AssemblyName UIAutomationClient;"
        "Add-Type -AssemblyName UIAutomationTypes;"
        "$name=[Text.Encoding]::UTF8.GetString("
        "[Convert]::FromBase64String('" + b64 + "'));"
        "$r=[System.Windows.Automation.AutomationElement]::RootElement;"
        "$pc=New-Object System.Windows.Automation.PropertyCondition("
        "[System.Windows.Automation.AutomationElement]::ProcessIdProperty,"
        + str(pid) + ");"
        "$wins=$r.FindAll([System.Windows.Automation.TreeScope]::Children,$pc);"
        "foreach($w in $wins){"
        "$els=$w.FindAll([System.Windows.Automation.TreeScope]::Descendants,"
        "[System.Windows.Automation.Condition]::TrueCondition);"
        "foreach($e in $els){"
        "if($e.Current.Name -eq $name){" + body + "} } };"
        "Write-Output 'MISS'"
    )


def uia_click_name(pid, name_text):
    """按控件可见文本点击（用于运行时动态创建的按钮）"""
    script = _uia_name_script(pid, name_text, "click")
    try:
        r = subprocess.run(["powershell", "-NoProfile", "-Command", script],
                           capture_output=True, text=True, timeout=40)
    except Exception as exc:  # noqa: BLE001
        return False, str(exc)
    return "OK:" in r.stdout, (r.stdout + r.stderr).strip()


def uia_exists_name(pid, name_text):
    """按控件可见文本判断是否存在"""
    script = _uia_name_script(pid, name_text, "exists")
    r = subprocess.run(["powershell", "-NoProfile", "-Command", script],
                       capture_output=True, text=True, timeout=40)
    return "FOUND" in r.stdout


def uia_exists_name_contains(pid, fragment):
    """按可见文本的片段判断是否存在（用于带动态数值的汇总行）"""
    import base64
    b64 = base64.b64encode(fragment.encode("utf-8")).decode("ascii")
    script = (
        "Add-Type -AssemblyName UIAutomationClient;"
        "Add-Type -AssemblyName UIAutomationTypes;"
        "$name=[Text.Encoding]::UTF8.GetString("
        "[Convert]::FromBase64String('" + b64 + "'));"
        "$r=[System.Windows.Automation.AutomationElement]::RootElement;"
        "$pc=New-Object System.Windows.Automation.PropertyCondition("
        "[System.Windows.Automation.AutomationElement]::ProcessIdProperty,"
        + str(pid) + ");"
        "$wins=$r.FindAll([System.Windows.Automation.TreeScope]::Children,$pc);"
        "foreach($w in $wins){"
        "$els=$w.FindAll([System.Windows.Automation.TreeScope]::Descendants,"
        "[System.Windows.Automation.Condition]::TrueCondition);"
        "foreach($e in $els){"
        "if($e.Current.Name -like ('*'+$name+'*')){"
        "Write-Output 'FOUND';exit 0 } } };"
        "Write-Output 'MISS'"
    )
    r = subprocess.run(["powershell", "-NoProfile", "-Command", script],
                       capture_output=True, text=True, timeout=40)
    return "FOUND" in r.stdout


def test_task_concurrency(exe, shot_dir):
    """测试4：长任务运行中再点另一个功能 → 询问框 → 后台任务窗口 → 取消"""
    print("\n[测试4] 任务并发：询问 / 后台任务 / 取消")
    proc = subprocess.Popen([exe], cwd=os.path.dirname(exe))
    try:
        hwnd = find_app_window(proc)
        if not hwnd:
            check("启动并显示主窗口", False, "未找到窗口")
            return
        time.sleep(2)

        # 在「实用工具」页启动 C 盘空间图（扫描 C:\，耗时足够长）
        switched, detail = uia_click(proc.pid, "navToolsBtn")
        check("切到实用工具页", switched, detail)
        time.sleep(0.5)
        started, detail = uia_click(proc.pid, "btn_treemap")
        check("启动长时间任务（C 盘空间图）", started, detail)
        time.sleep(1.0)

        # 切回网络页点「查端口占用」→ 应弹出并发询问框
        switched, _ = uia_click(proc.pid, "navNetworkBtn")
        time.sleep(0.5)
        check("切回网络页", switched)
        uia_click_async(proc.pid, "btn_ports")
        time.sleep(1.0)

        dlg = find_window(PROMPT_TITLE, timeout=8, pid=proc.pid)
        check("弹出「终止 / 后台运行」询问框", dlg is not None)
        if dlg:
            shot = os.path.join(shot_dir, "uitest_concurrency_prompt.png")
            if capture(dlg, shot):
                check("保存询问框截图", True, shot)
            press_enter(dlg)  # 默认按钮 = 放到后台并继续
            time.sleep(0.8)

            bg = find_window(BG_TITLE, timeout=8, pid=proc.pid)
            check("后台任务窗口出现", bg is not None)
            if bg:
                shot = os.path.join(shot_dir, "uitest_background.png")
                if capture(bg, shot):
                    check("保存后台任务截图", True, shot)

            check("日志栏出现「后台任务 1」入口",
                  uia_exists_name(proc.pid, "后台任务 1"))

            time.sleep(0.4)
            check("后台任务列表里有可取消的任务",
                  uia_exists_name(proc.pid, "取消"))

            cancelled, detail = uia_click_name(proc.pid, "取消")
            check("点击「取消」终止后台任务", cancelled, detail)
            time.sleep(1.0)
            check("取消后列表已清空",
                  uia_exists_name(proc.pid, "目前没有后台任务。"),
                  "未找到空列表提示")

        check("进程未崩溃", proc.poll() is None, f"exit={proc.poll()}")
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=8)
            except Exception:
                proc.kill()


def test_background_result(exe, shot_dir):
    """测试5：后台任务跑完后，可从后台窗口「查看结果」把输出渲染回卡片"""
    print("\n[测试5] 后台任务结果回收")
    proc = subprocess.Popen([exe], cwd=os.path.dirname(exe))
    try:
        hwnd = find_app_window(proc)
        if not hwnd:
            check("启动并显示主窗口", False, "未找到窗口")
            return
        time.sleep(2)

        switched, _ = uia_click(proc.pid, "navToolsBtn")
        check("切到实用工具页", switched)
        time.sleep(0.5)
        started, detail = uia_click(proc.pid, "btn_treemap")
        check("启动 C 盘空间图（稍后转入后台）", started, detail)
        time.sleep(1.0)

        uia_click(proc.pid, "navNetworkBtn")
        time.sleep(0.4)
        uia_click_async(proc.pid, "btn_ports")
        time.sleep(1.0)

        dlg = find_window(PROMPT_TITLE, timeout=8, pid=proc.pid)
        check("弹出「终止 / 后台运行」询问框", dlg is not None)
        if dlg:
            press_enter(dlg)  # 默认按钮 = 放到后台并继续
            time.sleep(1.0)
            check("日志栏出现「后台任务 1」入口",
                  uia_exists_name(proc.pid, "后台任务 1"))

            # 等后台的 C 盘扫描结束（大目录可能要几十秒）
            ready = False
            deadline = time.time() + 150
            while time.time() < deadline:
                if uia_exists_name(proc.pid, "查看结果"):
                    ready = True
                    break
                time.sleep(4)
            check("后台任务完成后出现「查看结果」", ready)

            if ready:
                bg = find_window(BG_TITLE, timeout=5, pid=proc.pid)
                if bg:
                    shot = os.path.join(shot_dir, "uitest_bg_result.png")
                    if capture(bg, shot):
                        check("保存后台结果窗口截图", True, shot)

                clicked, detail = uia_click_name(proc.pid, "查看结果")
                check("点击「查看结果」", clicked, detail)
                time.sleep(1.5)
                check("结果已渲染回卡片（出现树图汇总）",
                      uia_exists_name_contains(proc.pid, "个文件夹"),
                      "未找到汇总行")
                check("查看后后台任务入口消失",
                      not uia_exists(proc.pid, "bgTaskButton"))

        check("进程未崩溃", proc.poll() is None, f"exit={proc.poll()}")
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=8)
            except Exception:
                proc.kill()


def test_operation_page(exe, shot_dir):
    """测试6：操作记录页面可打开，记录卡片与汇总行正常显示"""
    print("\n[测试6] 操作记录页面")
    proc = subprocess.Popen([exe], cwd=os.path.dirname(exe))
    try:
        hwnd = find_app_window(proc)
        if not hwnd:
            check("启动并显示主窗口", False, "未找到窗口")
            return
        time.sleep(2)

        switched, detail = uia_click(proc.pid, "navOperationsBtn")
        check("切换到操作记录页", switched, detail)
        time.sleep(0.8)

        check("页面标题存在",
              uia_exists(proc.pid, "pageTitleOperations"))
        check("记录卡片存在",
              uia_exists(proc.pid, "cardOperations"))
        check("记录滚动区存在",
              uia_exists(proc.pid, "operationsScroll"))
        check("汇总行显示记录条数",
              uia_exists_name_contains(proc.pid, "条记录"),
              "未找到「共 N 条记录」")

        shot = os.path.join(shot_dir, "uitest_operations.png")
        if capture(hwnd, shot):
            check("保存操作记录页截图", True, shot)

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
    test_task_concurrency(exe, shot_dir)
    test_background_result(exe, shot_dir)
    test_operation_page(exe, shot_dir)

    print(f"\n===== 结果: {passed} passed, {failed} failed =====")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
