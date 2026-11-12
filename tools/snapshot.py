"""等待一次完整刷新后短暂停核，读回像素与 Q 表，再恢复 MCU。

需要 Pillow。输出图像来自 MCU 帧缓冲，不是物理 LCD 照片。
"""
import argparse
import json
import struct
import time
from pathlib import Path
from PIL import Image
from stlink import ROOT, STLink, symbols
from monitor import sample

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, default=ROOT/'logs/board_frame.png')
    args = parser.parse_args()
    syms = symbols()
    raw = ROOT/'logs/framebuffer.bin'
    qraw = ROOT/'logs/q_table.bin'
    with STLink() as link:
        initial = sample(link, syms['g_diag'])['frames']
        for _ in range(100):
            status = sample(link, syms['g_diag'])
            if status['frames'] != initial:
                break
            time.sleep(0.005)
        else:
            raise RuntimeError('Framebuffer is not advancing')
        link.command('halt')
        try:
            link.command(f'dump_image {{{raw.as_posix()}}} {syms["g_framebuffer"]:#x} 153600')
            # QLearner 的第一个成员是 Q 表：16*16*4 个 float，共 4096 字节。
            link.command(f'dump_image {{{qraw.as_posix()}}} {syms["g_agent"]:#x} 4096')
        finally:
            link.command('resume')
    # Cortex-M7 为小端；H 是 uint16_t，解码出 RGB565 后扩展到 8 位 RGB。
    pixels = struct.unpack('<76800H', raw.read_bytes())
    rgb = bytes(channel for value in pixels for channel in (
        ((value >> 11) & 31)*255//31,
        ((value >> 5) & 63)*255//63,
        (value & 31)*255//31))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    Image.frombytes('RGB', (240,320), rgb).save(args.output)
    values = struct.unpack('<1024f', qraw.read_bytes())
    table = [list(values[s*4:s*4+4]) for s in range(256)]
    (ROOT/'logs/q_table.json').write_text(json.dumps(table, indent=2), encoding='utf-8')
    print(json.dumps({'image': str(args.output), 'diagnostics': status}, indent=2))

if __name__ == '__main__':
    main()
