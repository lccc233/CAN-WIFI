# SPDX-License-Identifier: GPL-2.0-or-later
"""Build a self-contained Windows x64 flasher from the current ESP-IDF output.

Run with the ESP-IDF Python site-packages on PYTHONPATH and PyInstaller installed.
No mailbox credentials or NVS dump is included in the output.
"""
import argparse
import hashlib
import importlib.metadata
import json
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile


ROOT = Path(__file__).resolve().parent.parent
NAME = 'CAN-WIFI_20261008_RECORDING_Win64'
RELEASE = '2026.10.08 完整记录版'
DOCUMENTS = ['serial-protocol.md', 'can-sine-test.md', 'offline-flashing.md',
             'recording.md', 'recording-stress.md']
HELPERS = ['can-sine-test.py', 'requirements-can-test.txt', 'provision-mail.py',
           'can-recording-stress.py', 'verify-recording-stress.py']


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def clean_source_commit():
    pending = subprocess.check_output(
        ['git', 'status', '--porcelain', '--untracked-files=all'], cwd=ROOT, text=True).strip()
    if pending:
        raise RuntimeError('Source tree has tracked or untracked changes; commit them before packaging')
    return subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build-mail')
    args = parser.parse_args()
    source_commit = clean_source_commit()
    import esptool
    release = ROOT / 'release'
    package = release / NAME
    if package.exists() and any(package.iterdir()):
        raise RuntimeError('Output package directory is not empty; use a clean directory to avoid bundling stale files')
    firmware = package / 'firmware'
    firmware.mkdir(parents=True, exist_ok=True)
    files = [('bootloader.bin', 'bootloader/bootloader.bin', 0x0),
             ('partition-table.bin', 'partition_table/partition-table.bin', 0x8000),
             ('can_monitor.bin', 'can_monitor.bin', 0x10000)]
    flash_args = json.loads((args.build_dir / 'flasher_args.json').read_text())
    actual = {int(offset, 16): path.replace('\\', '/') for offset, path in flash_args['flash_files'].items()}
    if actual != {offset: source for _, source, offset in files}:
        raise RuntimeError('Build flash layout differs from the verified NVS-preserving layout')
    if flash_args['flash_settings'] != {'flash_mode': 'dio', 'flash_size': '16MB', 'flash_freq': '80m'}:
        raise RuntimeError('Unexpected flash configuration')
    manifest = {'release': RELEASE, 'chip': 'esp32s3', 'module': 'N16R8',
                'flash_mode': 'dio', 'flash_freq': '80m', 'flash_size': '16MB',
                'can_tx': 5, 'can_rx': 4, 'can_bitrate': 250000,
                'idf_version': '5.3.5', 'esptool_version': esptool.__version__,
                'preserve_nvs': {'offset': '0x9000', 'size': '0x6000'}, 'images': []}
    manifest['source_commit'] = source_commit
    manifest['source_dirty'] = False
    for name, source, offset in files:
        target = firmware / name
        shutil.copy2(args.build_dir / source, target)
        manifest['images'].append({'file': name, 'offset': hex(offset),
                                   'size': target.stat().st_size, 'sha256': digest(target)})
    (firmware / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    command = [sys.executable, '-m', 'PyInstaller', '--onefile', '--windowed', '--clean', '--noconfirm',
               '--name', 'CAN-WIFI-Flasher', '--collect-all', 'esptool',
               '--paths', str(Path(esptool.__file__).resolve().parent.parent),
               '--add-data', str(firmware) + ';firmware',
               '--distpath', str(package), '--workpath', str(release / '_offline_build'),
               '--specpath', str(release / '_offline_spec'), str(ROOT / 'tools/offline-flasher.py')]
    subprocess.run(command, cwd=ROOT, check=True)
    docs = package / 'docs'
    docs.mkdir(exist_ok=True)
    for name in DOCUMENTS:
        shutil.copy2(ROOT / 'docs' / name, docs / name)
    shutil.copy2(ROOT / 'README.md', package / 'README.md')
    helpers = package / 'tools'
    helpers.mkdir(exist_ok=True)
    for name in HELPERS:
        shutil.copy2(ROOT / 'tools' / name, helpers / name)
    readme = """CAN-WIFI 独立烧录包 · 2026.10.08 完整记录版
========================================

适用：Windows 10/11 64位；ESP32-S3 N16R8（16MB Flash / 8MB八线PSRAM）。
烧录器内置 Python 运行时、esptool 和本版本的三段固件。
无需安装 Python、ESP-IDF、Node.js，不需要联网下载工具，不需要管理员权限。
电脑仍需识别开发板的 USB 串口；优先使用板载原生 USB-Serial/JTAG。
若使用 USB 转 UART 且未出现 COM 口，需要该芯片对应的串口驱动。

使用步骤
--------
1. 将整个 ZIP 解压到本地目录，不要直接在压缩包内运行。
2. 用 USB 数据线连接开发板，关闭占用串口的 monitor、VOFA 或其他上位机。
   烧录前先停止 Record，等待同步完整，再下载或邮件保存需要保留的记录。
3. 双击 CAN-WIFI-Flasher.exe，选择开发板串口（本机为 COM8，以实际识别为准）。
4. 默认速度 460800，点击“开始烧录”，保持 USB 连接。
5. 日志显示“烧录成功”后设备自动重启，启动可能因 PSRAM 测试等待数秒。

固件和工具已嵌入 EXE。仅复制 EXE 到其他目录也可以烧录。
firmware 文件夹是对应的原始固件与 SHA-256 清单，供核对或其他工具使用。
本工具固定写入三段固件，不执行整片擦除，也不覆盖 NVS。

方法二：乐鑫官方网页版烧录
--------------------------
1. 使用 Chrome 或 Edge 打开 https://espressif.github.io/esptool-js/ 。
2. 连接 USB 数据线，关闭占用开发板串口的软件。
3. 在 Program 区域选择 Baudrate=460800，点 Connect，选择开发板串口，确认芯片为 ESP32-S3。
4. 点 Add File，添加三行，填写 Flash Address 并选择本包 firmware 文件夹的对应 BIN：
   0x0      firmware/bootloader.bin
   0x8000   firmware/partition-table.bin
   0x10000  firmware/can_monitor.bin
5. 选择 Flash Mode=dio、Flash Frequency=80MHz、Flash Size=16MB，点 Program。
6. 等全部文件写入完成且日志无错误后，点 Disconnect，再按开发板 RESET 重启。
保留已有 WiFi、邮箱授权和曲线配置时，不要点击 Erase Flash。
网页默认地址需按上述值修改；ESP32-S3 bootloader 从0x0开始。
连接失败可按住 BOOT、点按 RESET 后松开 BOOT，重试；不稳定可降为115200。
网页版选择三个 BIN，不选择 ZIP、EXE、ELF 或 JSON。
首次访问网页需加载在线资源；完全离线烧录使用本包 EXE。
详细说明见 docs/offline-flashing.md。

功能和默认值
------------
CAN：250kbps，NORMAL，TX=GPIO5，RX=GPIO4；WS2812=GPIO48。
WiFi：使用设备保存配置；全新设备默认 STA，SSID=ABCDEF，密码=A12345678。
STA 地址规则默认同网段主机号250；网页也可用 http://can-monitor.local。
AP 默认 CAN-Monitor-XXXX / 12345678，地址 http://192.168.4.1。
CSV 记录、导出、信封发邮件、串口邮箱配置和 status 邮箱字段均已包含。
发件邮箱 espdata@agent.qq.com；默认收件邮箱 lichen1435374410@163.com。
收件地址可用串口 mailto 命令修改。

完整记录与曲线
--------------
Record 开始后，设备将目标 CAN 原始帧写入独立 PSRAM 记录缓冲。
网页分批读取并统一解码，曲线和 CSV 共用本次解码结果；曲线窗口不限制完整记录。
信号定义在本次开始时冻结，后续配置修改仅用于下次录制。
网络卡顿或网页暂时断开时设备继续记录，恢复后可按帧序号补齐。
Stop 后自动核对并补齐，确认同步完整后才开放 CSV 下载和邮件发送。
容量受 PSRAM 和浏览器安全内存上限约束；设备录满时明确停止，不覆盖已有记录。
同步完整表示设备保存的帧已全部读取；接收丢失、录满及完整性未知会另行显示。
设备原始记录位于易失 PSRAM，断电、复位或烧录重启会丢失；浏览器缓存也不是持久存储。
需要保留的数据请及时下载或邮件发送，开始新记录前保存上一份记录。
详见 docs/recording.md。

五分钟负载验证
--------------
2026-10-08 实机测试：两路目标报文叠加 64 个背景 ID，运行五分钟。
全部实际发送及设备接收均为 419,999 帧，目标记录及 CSV 均为 35,999 帧，逐帧逐行一致。
PC 调度未发送：1 个电机时隙（独立统计，未发到总线，不属于设备漏收）。
设备 rx_lost=0、drop=0；本次未验证邮件并发及更长连续运行。
详见 docs/recording-stress.md。本包只包含测试工具与说明，不包含实测 CSV、
串口快照、私人日志、OAuth 凭据或设备 NVS。

升级与邮箱授权
--------------
在本项目原有分区布局上升级，保留 WiFi、曲线配置、收件地址和邮箱授权。
本包没有内置用户授权凭据，也没有复制任何设备 NVS；新板不会继承旧板邮箱授权。
新设备或授权失效时，仍需先完成 QQ Agent OAuth 授权，再用串口 mailauth 导入。
该首次授权流程与固件烧录独立，详见 docs/serial-protocol.md。
保留配置仅适用于本项目兼容布局；从其他项目迁移需自行核对已有分区。
设备 PSRAM 原始帧及浏览器记录缓存均不属于 NVS，烧录重启前请先保存需要保留的记录。

写入布局
--------
0x000000  bootloader.bin
0x008000  partition-table.bin
0x010000  can_monitor.bin（3MB应用分区）
NVS 0x009000～0x00EFFF 保留；不会写入此区域。
不要把三段 BIN 直接拼接为一个原始 BIN 从0地址烧录，填充区域会覆盖NVS。
可用乐鑫工具按以上三个地址分别烧录，参数 DIO / 80MHz / 16MB。

故障处理
--------
未出现串口：换 USB 数据线、检查设备管理器；纯充电线无法使用。
连接失败：关掉占用端口的软件，按住 BOOT、点按 RESET，再尝试烧录。
中途失败：保持连接并重试；可将烧录速度降为115200。
失败后不要使用整片擦除来排查，整片擦除会删除已保存配置与授权。
重新烧录了网页却看到旧界面：在确保记录已导出后，Ctrl+Shift+R 强制刷新浏览器。

命令行诊断（可选，结果写到指定UTF-8文件）
------------------------------------------
CAN-WIFI-Flasher.exe --verify-package --report check.json
CAN-WIFI-Flasher.exe --list-ports --report ports.txt
CAN-WIFI-Flasher.exe --flash COM8 --baud 460800 --report flash.log
GUI直接显示日志；命令行模式可在输出文件中查看结果，退出码0为成功。

源代码与许可
------------
source.zip 包含本包烧录器源码、打包脚本和所用 esptool Python 源码与资源。
licenses 中包含第三方许可；esptool 使用 GPL-2.0-or-later。
tools 目录中的两个烧录器/打包器源码按 GPL-2.0-or-later 提供。
docs 包含串口协议、正弦CAN测试、完整记录架构及五分钟负载验证说明。
tools 提供正弦测试、记录压力测试、核对器与首次邮箱授权导入脚本；
这些额外操作需要Python和相应依赖，不影响EXE独立烧录。
"""
    (package / '烧录说明.txt').write_text(readme, encoding='utf-8-sig')
    licenses = package / 'licenses'
    licenses.mkdir(exist_ok=True)
    notices = []
    for name in ['esptool', 'pyserial', 'cryptography', 'cffi', 'bitstring', 'bitarray',
                 'ecdsa', 'reedsolo', 'intelhex', 'PyYAML', 'PyInstaller',
                 'six', 'typing_extensions', 'pycparser', 'packaging']:
        distribution = importlib.metadata.distribution(name)
        notices.append(f'{name} {distribution.version}')
        for entry in distribution.files or []:
            if any(part.lower().startswith(('license', 'copying')) for part in entry.parts):
                path = distribution.locate_file(entry)
                if path.is_file():
                    destination = licenses / name / Path(*entry.parts)
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(path, destination)
    for path in [Path(sys.base_prefix) / 'LICENSE.txt',
                 Path(sys.base_prefix) / 'tcl/tcl8.6/license.terms',
                 Path(sys.base_prefix) / 'tcl/tk8.6/license.terms']:
        if path.is_file():
            destination = licenses / 'Python-Tcl-Tk' / path.parent.name / path.name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, destination)
    (package / 'THIRD-PARTY-NOTICES.txt').write_text('\n'.join(notices) + '\nPython runtime: https://docs.python.org/3/license.html\n', encoding='utf-8')
    with zipfile.ZipFile(package / 'source.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
        for name in ['offline-flasher.py', 'package-offline-firmware.py']:
            archive.write(ROOT / 'tools' / name, 'tools/' + name)
        esp_root = Path(esptool.__file__).resolve().parent
        for path in esp_root.rglob('*'):
            if path.is_file() and '__pycache__' not in path.parts and path.suffix != '.pyc':
                archive.write(path, 'esptool/' + path.relative_to(esp_root).as_posix())
        for path in licenses.rglob('*'):
            if path.is_file():
                archive.write(path, 'licenses/' + path.relative_to(licenses).as_posix())
    if clean_source_commit() != source_commit:
        raise RuntimeError('Source commit changed during packaging; rebuild from a clean committed tree')
    checksums = []
    for path in sorted(package.rglob('*')):
        if path.is_file() and path.name != 'SHA256SUMS.txt':
            checksums.append(digest(path) + '  ' + path.relative_to(package).as_posix())
    (package / 'SHA256SUMS.txt').write_text('\n'.join(checksums) + '\n', encoding='utf-8')
    output = release / (NAME + '.zip')
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(package.rglob('*')):
            if path.is_file():
                archive.write(path, NAME + '/' + path.relative_to(package).as_posix())
    print(f'Package: {output}\nSize: {output.stat().st_size} bytes\nSHA256: {digest(output)}')


if __name__ == '__main__':
    main()
