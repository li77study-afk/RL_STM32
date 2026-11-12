"""管理短期 OpenOCD 会话，通过 TCP/Tcl 向 ST-LINK 发送读写命令。

Python → 本机 OpenOCD → USB/ST-LINK → SWD → STM32 内存。
with STLink() 保证退出时释放连接；启动失败时请查看 logs/openocd_monitor.log。
"""
import re
import socket
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN = Path('D:/STM32Tools/arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi/bin')

def symbols():
    # 从本次 ELF 查符号地址，避免源文件变化后仍使用旧的硬编码 RAM 地址。
    # 必须确保板上固件与 ELF 匹配；magic/version 只能检查协议，不能检查每个地址。
    output = subprocess.check_output([str(TOOLCHAIN/'arm-none-eabi-nm.exe'), '-n', str(ROOT/'build/q_learning.elf')], text=True)
    return {m[2]: int(m[1], 16) for m in re.finditer(r'^([0-9a-fA-F]+) \w (\S+)$', output, re.M)}

class STLink:
    def __enter__(self):
        # 让系统分配临时回环 TCP 端口；关闭无需使用的 GDB/Telnet 端口。
        # 端口不同并不意味着可以同时占用同一个 ST-LINK，硬件访问仍应互斥。
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0))
            self.port = sock.getsockname()[1]
        (ROOT/'logs').mkdir(exist_ok=True)
        self.log = (ROOT/'logs/openocd_monitor.log').open('w')
        self.process = subprocess.Popen([
            'D:/STM32Tools/openocd/bin/openocd.exe',
            '-s', 'D:/STM32Tools/openocd/share/openocd/scripts',
            '-f', 'interface/stlink.cfg', '-f', 'target/stm32h7x.cfg',
            '-c', 'adapter speed 4000', '-c', 'gdb_port disabled',
            '-c', 'telnet_port disabled', '-c', f'tcl_port {self.port}',
        ], stdout=self.log, stderr=subprocess.STDOUT,
           creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        # 等待 OpenOCD 初始化并开始监听，而不是假定启动子进程后立即可连接。
        for _ in range(100):
            if self.process.poll() is not None:
                self.log.close()
                raise RuntimeError('OpenOCD failed; see logs/openocd_monitor.log. Close other ST-LINK sessions.')
            try:
                self.sock = socket.create_connection(('127.0.0.1', self.port), timeout=0.2)
                self.sock.settimeout(10)
                return self
            except OSError:
                time.sleep(0.1)
        self.process.terminate(); self.process.wait(); self.log.close()
        raise TimeoutError('OpenOCD did not start')

    def command(self, command):
        # OpenOCD Tcl RPC 使用 0x1A 分隔消息；TCP 一次 recv 不保证收完整回复。
        self.sock.sendall(command.encode() + b'\x1a')
        data = b''
        while not data.endswith(b'\x1a'):
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError('OpenOCD disconnected')
            data += chunk
        return data[:-1].decode()

    def words(self, address, count):
        # 32 表示每项 32 位；count 为字数，不是字节数。
        return [int(v, 0) for v in self.command(f'read_memory {address:#x} 32 {count}').split()]

    def __exit__(self, *exc):
        # 优先用协议正常退出，只有进程迟迟不退出时才终止自己创建的子进程。
        try:
            self.command('shutdown')
        except (OSError, ConnectionError):
            pass
        self.sock.close()
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.terminate(); self.process.wait()
        self.log.close()
