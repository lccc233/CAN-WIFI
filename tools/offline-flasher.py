# SPDX-License-Identifier: GPL-2.0-or-later
"""Standalone Windows ESP32-S3 flasher; freeze with package-offline-firmware.py."""
import argparse
import contextlib
import hashlib
import io
import json
from pathlib import Path
import queue
import re
import sys
import threading

import esptool
from serial.tools import list_ports


def bundle_root():
    return Path(getattr(sys, '_MEIPASS', Path(__file__).resolve().parent))


def verified_images():
    root = bundle_root() / 'firmware'
    manifest = json.loads((root / 'manifest.json').read_text(encoding='utf-8'))
    expected = {'bootloader.bin': (0x0, 0x8000),
                'partition-table.bin': (0x8000, 0x1000),
                'can_monitor.bin': (0x10000, 0x300000)}
    images = []
    entries = manifest['images']
    if len(entries) != 3 or {item['file'] for item in entries} != expected.keys():
        raise RuntimeError('固件清单不完整，请重新解压烧录包。')
    for item in entries:
        offset, maximum = expected[item['file']]
        path = root / item['file']
        data = path.read_bytes()
        if (int(item['offset'], 16) != offset or not 0 < len(data) <= maximum
                or len(data) != item['size']
                or hashlib.sha256(data).hexdigest() != item['sha256']):
            raise RuntimeError(f"固件校验失败：{item['file']}，请重新获取烧录包。")
        images.append((offset, path))
    return manifest, sorted(images)


def flash(port, baud):
    if not re.fullmatch(r'COM[1-9][0-9]*', port.upper()):
        raise RuntimeError('请选择有效串口，例如 COM8。')
    manifest, images = verified_images()
    print(f"固件：{manifest['release']}；ESP32-S3 N16R8；CAN TX=5 RX=4，250kbps", flush=True)
    print('保留 NVS 配置。烧录期间请保持 USB 连接，完成后设备自动重启。', flush=True)
    arguments = ['--chip', 'esp32s3', '--port', port.upper(), '--baud', str(baud),
                 '--before', 'default_reset', '--after', 'hard_reset', 'write_flash',
                 '--flash_mode', 'dio', '--flash_freq', '80m', '--flash_size', '16MB']
    for offset, path in images:
        arguments.extend([hex(offset), str(path)])
    esptool.main(arguments)
    print('\n烧录成功：三段固件已写入并校验，设备已重启。', flush=True)


class QueueOutput:
    encoding = 'utf-8'
    errors = 'replace'
    def __init__(self, events):
        self.events = events
    def write(self, value):
        if value:
            self.events.put(('log', value))
        return len(value)
    def flush(self):
        pass
    def isatty(self):
        return False


class FlasherWindow:
    def __init__(self, root):
        import tkinter as tk
        from tkinter import ttk
        from tkinter.scrolledtext import ScrolledText
        self.root = root
        self.events = queue.Queue()
        self.busy = False
        manifest, _ = verified_images()
        root.title(f"CAN-WIFI 离线烧录器 · {manifest['release']}")
        root.geometry('860x570')
        root.minsize(740, 480)
        root.protocol('WM_DELETE_WINDOW', self.close)
        frame = ttk.Frame(root, padding=18)
        frame.pack(fill='both', expand=True)
        ttk.Label(frame, text='CAN-WIFI 固件烧录', font=('Microsoft YaHei UI', 17, 'bold')).pack(anchor='w')
        ttk.Label(frame, text='ESP32-S3 N16R8 · 16MB Flash / 8MB PSRAM · CAN TX=GPIO5 / RX=GPIO4 · 250kbps').pack(anchor='w', pady=(8, 4))
        ttk.Label(frame, text='内置固件与烧录工具，可离线使用；升级保留 WiFi、邮箱授权及曲线配置。').pack(anchor='w')
        ttk.Label(frame, text='烧录前请停止记录、等待同步完整并保存 CSV；重启会清空设备 PSRAM 原始记录。').pack(anchor='w')
        row = ttk.Frame(frame)
        row.pack(fill='x', pady=16)
        ttk.Label(row, text='设备串口').pack(side='left')
        self.port = tk.StringVar()
        self.ports = ttk.Combobox(row, textvariable=self.port, width=12)
        self.ports.pack(side='left', padx=(8, 6))
        self.refresh_button = ttk.Button(row, text='刷新串口', command=self.refresh)
        self.refresh_button.pack(side='left')
        ttk.Label(row, text='烧录速度').pack(side='left', padx=(20, 8))
        self.baud = tk.StringVar(value='460800')
        self.bauds = ttk.Combobox(row, textvariable=self.baud, values=['115200', '460800', '921600'], state='readonly', width=10)
        self.bauds.pack(side='left')
        self.start_button = ttk.Button(row, text='开始烧录', command=self.start)
        self.start_button.pack(side='right')
        self.status = tk.StringVar(value='连接开发板，选择串口后点击“开始烧录”。')
        ttk.Label(frame, textvariable=self.status).pack(anchor='w', pady=(0, 6))
        self.progress = ttk.Progressbar(frame, mode='indeterminate')
        self.progress.pack(fill='x', pady=(0, 10))
        self.log = ScrolledText(frame, wrap='word', font=('Consolas', 10), height=16)
        self.log.pack(fill='both', expand=True)
        self.log.configure(state='disabled')
        ttk.Label(frame, text='请先关闭串口终端。若连接失败：换数据线，或按住 BOOT、点按 RESET 后重试；不稳定时选 115200。').pack(anchor='w', pady=(10, 0))
        self.refresh()
        self.append(f"固件 SHA-256 校验通过：{manifest['release']}\n仅写入 bootloader、分区表、应用，保留 0x9000～0xEFFF 的 NVS。\n")
        root.after(60, self.poll)

    def append(self, value):
        self.log.configure(state='normal')
        self.log.insert('end', value.replace('\r', '\n'))
        self.log.see('end')
        self.log.configure(state='disabled')

    def refresh(self):
        ports = list(list_ports.comports())
        ports.sort(key=lambda item: (item.vid != 0x303A, item.device != 'COM8', item.device))
        values = [item.device for item in ports]
        self.ports['values'] = values
        if self.port.get() not in values:
            self.port.set(values[0] if values else '')
        if not values:
            self.status.set('未检测到串口，请连接 USB 数据线，再点“刷新串口”。')

    def start(self):
        from tkinter import messagebox
        if self.busy:
            return
        port = self.port.get().strip().upper()
        if not re.fullmatch(r'COM[1-9][0-9]*', port):
            messagebox.showerror('请选择串口', '连接开发板并选择有效串口，例如 COM8。', parent=self.root)
            return
        baud = int(self.baud.get())
        self.busy = True
        self.start_button.configure(state='disabled')
        self.refresh_button.configure(state='disabled')
        self.ports.configure(state='disabled')
        self.bauds.configure(state='disabled')
        self.status.set(f'正在烧录 {port}，请保持连接…')
        self.progress.start(12)
        self.append(f'\n开始：{port}，速度 {baud}\n')
        threading.Thread(target=self.worker, args=(port, baud), daemon=True).start()

    def worker(self, port, baud):
        output = QueueOutput(self.events)
        try:
            with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
                flash(port, baud)
        except BaseException as error:
            self.events.put(('error', f'{type(error).__name__}: {error}'))
        else:
            self.events.put(('success', None))

    def poll(self):
        from tkinter import messagebox
        try:
            while True:
                kind, value = self.events.get_nowait()
                if kind == 'log':
                    self.append(value)
                    continue
                self.busy = False
                self.progress.stop()
                self.start_button.configure(state='normal')
                self.refresh_button.configure(state='normal')
                self.ports.configure(state='normal')
                self.bauds.configure(state='readonly')
                if kind == 'success':
                    self.status.set('烧录成功，设备已重启。可关闭本窗口。')
                else:
                    self.status.set('烧录失败，请查看日志并检查连接。')
                    self.append('\n烧录失败：' + value + '\n')
                    messagebox.showerror('烧录失败', value + '\n检查串口占用或 USB 接线；可改为 115200 重试。', parent=self.root)
        except queue.Empty:
            pass
        self.root.after(60, self.poll)

    def close(self):
        from tkinter import messagebox
        if self.busy:
            messagebox.showinfo('烧录进行中', '请等待烧录结束后关闭，避免中断写入。', parent=self.root)
            return
        self.root.destroy()


def main():
    parser = argparse.ArgumentParser(description='CAN-WIFI 独立烧录器；无参数启动图形界面。')
    parser.add_argument('--verify-package', action='store_true', help='校验内嵌固件，不连接设备')
    parser.add_argument('--list-ports', action='store_true', help='列出串口，不连接设备')
    parser.add_argument('--flash', metavar='COM', help='命令行烧录，例如 COM8')
    parser.add_argument('--baud', type=int, choices=[115200, 460800, 921600], default=460800)
    parser.add_argument('--report', type=Path, help='诊断输出保存到 UTF-8 文件（用于命令行模式）')
    parser.add_argument('--ui-self-test', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.report:
        sys.stdout = sys.stderr = args.report.open('w', encoding='utf-8', buffering=1)
    if args.verify_package:
        manifest, _ = verified_images()
        print(json.dumps(manifest, ensure_ascii=False, indent=2))
    elif args.list_ports:
        for port in list_ports.comports():
            print(f'{port.device}\t{port.description}')
    elif args.flash:
        flash(args.flash, args.baud)
    else:
        import tkinter as tk
        root = tk.Tk()
        if args.ui_self_test:
            root.withdraw()
        FlasherWindow(root)
        if args.ui_self_test:
            root.update()
            root.destroy()
            print('GUI initialization OK')
        else:
            root.mainloop()
    return 0


if __name__ == '__main__':
    # A windowed frozen executable has no attached console.
    if sys.stdout is None:
        sys.stdout = io.StringIO()
    if sys.stderr is None:
        sys.stderr = io.StringIO()
    try:
        sys.exit(main())
    except Exception as error:
        print(f'错误：{error}', file=sys.stderr)
        if len(sys.argv) == 1:
            import tkinter.messagebox
            tkinter.messagebox.showerror('烧录器错误', str(error))
        sys.exit(1)
