"""Import this mailbox's OAuth refresh credential into an ESP32 via serial.

Windows only: credentials remain encrypted by DPAPI on disk and are never printed.
The computer is needed only for first-time authorization/provisioning.
"""
import argparse
import base64
import ctypes
import json
import shutil
import subprocess
import sys
from pathlib import Path

SENDER = "espdata@agent.qq.com"


class DataBlob(ctypes.Structure):
    _fields_ = [("size", ctypes.c_ulong), ("data", ctypes.POINTER(ctypes.c_ubyte))]


def unprotect(data):
    buffer = (ctypes.c_ubyte * len(data)).from_buffer_copy(data)
    source = DataBlob(len(data), buffer)
    target = DataBlob()
    if not ctypes.windll.crypt32.CryptUnprotectData(
        ctypes.byref(source), None, None, None, None, 0, ctypes.byref(target)
    ):
        raise RuntimeError("DPAPI 解密失败，请使用完成邮箱授权的 Windows 用户运行。")
    try:
        return ctypes.string_at(target.data, target.size)
    finally:
        ctypes.windll.kernel32.LocalFree(target.data)


def cli_json(cli, *args):
    result = subprocess.run([cli, *args], capture_output=True, encoding="utf-8")
    if result.returncode:
        raise RuntimeError("邮箱 CLI 检查失败，请先运行 agently-cli auth login。")
    # Ignore optional human-readable tips after the JSON envelope.
    return json.JSONDecoder().raw_decode(result.stdout.lstrip())[0]


def read_authorization(cli):
    import winreg
    status = cli_json(cli, "auth", "status")["data"]
    if not status.get("logged_in"):
        raise RuntimeError("请先使用 agently-cli auth login 完成邮箱授权。")
    aliases = cli_json(cli, "+me")["data"]["aliases"]
    if not any(a.get("email") == SENDER for a in aliases):
        raise RuntimeError("授权邮箱不是 " + SENDER)
    workspace = status["workspace"]
    if not workspace or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-" for c in workspace):
        raise RuntimeError("不支持的 CLI workspace 格式。")
    # Matches official CLI 1.0.18's Windows DPAPI store, not an API promise.
    key_path = "Software\\AgentlyCli\\keychain\\agently-cli\\agents\\" + workspace
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key_path) as key:
        encrypted, _ = winreg.QueryValueEx(key, base64.b64encode(b"bootstrap_token").decode())
    plain = unprotect(base64.b64decode(encrypted))
    # CLI places a storage format prefix before the JSON credential object.
    start, end = plain.find(b"{"), plain.rfind(b"}")
    token = json.loads(plain[start:end + 1])
    return {"email": SENDER, "client_id": status["app_id"], "refresh_token": token["refresh_token"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="ESP32 serial port, e.g. COM8")
    parser.add_argument("--cli", help="Path to agently-cli.exe or installed CLI")
    parser.add_argument("--auth-file", type=Path, help="Local DPAPI-encrypted provisioning file")
    args = parser.parse_args()
    if sys.platform != "win32":
        raise RuntimeError("此凭据导入工具仅支持 Windows DPAPI；其他平台可通过 mailauth 手工导入。")
    if args.auth_file:
        auth = json.loads(unprotect(args.auth_file.read_bytes()))
    else:
        cli = args.cli or shutil.which("agently-cli.exe") or shutil.which("agently-cli.cmd") or shutil.which("agently-cli")
        if not cli:
            raise RuntimeError("请先安装官方 CLI：npm install -g @tencent-qqmail/agently-cli")
        auth = read_authorization(cli)
    if auth.get("email") != SENDER or not auth.get("client_id") or not auth.get("refresh_token"):
        raise RuntimeError("邮箱授权配置无效。")
    command = ("mailauth " + json.dumps(auth, separators=(",", ":")) + "\r\n").encode()
    if len(command) >= 2048:
        raise RuntimeError("凭据超过设备串口命令长度，请检查 CLI 版本。")
    import serial
    import time
    # Suppress reset lines before opening the port; do not reboot a running logger.
    port = serial.Serial()
    port.port = args.port
    port.baudrate = 115200
    port.timeout = 0.1
    port.write_timeout = 5
    port.dtr = False
    port.rts = False
    with port:
        port.reset_input_buffer()
        # Device reads USB FIFO every 20ms. Pace chunks to prevent UART overruns.
        for offset in range(0, len(command), 32):
            port.write(command[offset:offset + 32])
            port.flush()
            time.sleep(0.03)
        deadline = time.monotonic() + 8
        response = bytearray()
        while time.monotonic() < deadline:
            response.extend(port.read(256))
            if b"MAILAUTH OK" in response:
                print("设备邮箱授权已保存：" + SENDER)
                return
            if b"MAILAUTH ERROR" in response:
                raise RuntimeError("设备拒绝授权配置，请检查固件版本及设备发送状态。")
        raise RuntimeError("未收到设备确认，请检查串口是否正确、固件是否更新。")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        # Do not expose subprocess output, credentials, or raw serial data.
        if isinstance(error, RuntimeError): print(str(error), file=sys.stderr)
        else: print("邮箱配置失败（" + type(error).__name__ + "），请检查配置与连接。", file=sys.stderr)
        sys.exit(1)
