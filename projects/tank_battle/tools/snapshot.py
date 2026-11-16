"""请求应用暂时冻结逻辑/帧缓冲，读回 LCD 发送像素，完成后自动恢复。"""
import argparse
import json
import time
from pathlib import Path
import numpy as np
from PIL import Image
from stlink import ROOT,STLink,symbols
from monitor import sample,ready

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,default=ROOT/'logs/frame.png')
    parser.add_argument('--wait-bounce',action='store_true',help='Wait up to 30 seconds for a live ricocheting bullet')
    parser.add_argument('--wait-multi',action='store_true',help='Wait for at least two live bullets from one tank')
    args=parser.parse_args()
    syms=symbols();raw=ROOT/'logs/frame.bin'
    with STLink() as link:
        ready(link,syms['g_diag'])
        try:
            if args.wait_bounce or args.wait_multi:
                deadline=time.monotonic()+30
                while True:
                    current=sample(link,syms['g_diag'])
                    bouncing=(current['blue_bullet_active'] and current['blue_bullet_collisions']>0) or (current['red_bullet_active'] and current['red_bullet_collisions']>0)
                    multi=current['blue_active_bullets']>=2 or current['red_active_bullets']>=2
                    if (not args.wait_bounce or bouncing) and (not args.wait_multi or multi):break
                    if time.monotonic()>deadline:raise TimeoutError('Requested bullet condition not seen in 30 seconds')
                    time.sleep(0.05)
            link.command(f'write_memory {syms["g_command"]:#x} 32 {{2}}');deadline=time.monotonic()+5
            while True:
                status=sample(link,syms['g_diag'])
                if status['frozen']:break
                if time.monotonic()>deadline:raise TimeoutError('Freeze timeout')
                time.sleep(0.01)
            link.command(f'dump_image {{{raw.as_posix()}}} {syms["g_framebuffer"]:#x} 153600')
        finally:
            link.command(f'write_memory {syms["g_command"]:#x} 32 {{3}}');time.sleep(0.2)
    values=np.frombuffer(raw.read_bytes(),dtype='<u2').reshape(320,240)
    rgb=np.stack([((values>>11)&31)*255//31,((values>>5)&63)*255//63,(values&31)*255//31],axis=-1).astype(np.uint8)
    args.output.parent.mkdir(parents=True,exist_ok=True);Image.fromarray(rgb).save(args.output)
    args.output.with_suffix('.json').write_text(json.dumps(status,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(status),flush=True)

if __name__=='__main__':main()
