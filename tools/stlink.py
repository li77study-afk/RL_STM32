"""Short-lived OpenOCD connection. No IDE or USB serial adapter required."""
import re
import socket
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN = Path('D:/STM32Tools/arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi/bin')

def symbols():
    output = subprocess.check_output([str(TOOLCHAIN/'arm-none-eabi-nm.exe'), '-n', str(ROOT/'build/q_learning.elf')], text=True)
    return {m[2]: int(m[1], 16) for m in re.finditer(r'^([0-9a-fA-F]+) \w (\S+)$', output, re.M)}

class STLink:
    def __enter__(self):
        # An ephemeral loopback port avoids interfering with a user's debug server.
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
        self.sock.sendall(command.encode() + b'\x1a')
        data = b''
        while not data.endswith(b'\x1a'):
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError('OpenOCD disconnected')
            data += chunk
        return data[:-1].decode()

    def words(self, address, count):
        return [int(v, 0) for v in self.command(f'read_memory {address:#x} 32 {count}').split()]

    def __exit__(self, *exc):
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
