"""保存板上实际相机预览和 28×28 CNN 输入，供检查裁剪、对焦和背景处理。

冻结的是 CPU 副本；相机仍采集，CPU 不停机。finally 始终请求恢复预览。
"""
import argparse
import json
import time
from pathlib import Path
import numpy as np
from PIL import Image
from stlink import ROOT,STLink,symbols
from monitor import sample,wait_ready

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--prefix',type=Path,default=ROOT/'logs/capture');args=parser.parse_args()
    syms=symbols();args.prefix.parent.mkdir(parents=True,exist_ok=True)
    raw=ROOT/'logs/capture_rgb565.bin';digit=ROOT/'logs/capture_input.bin'
    with STLink() as link:
        wait_ready(link,syms['g_status'])
        try:
            link.command(f'write_memory {syms["g_command"]:#x} 32 {{3}}')
            deadline=time.monotonic()+5
            while True:
                status=sample(link,syms['g_status'])
                if status['frame_frozen']:break
                if time.monotonic()>deadline:raise TimeoutError('Frame freeze timeout')
                time.sleep(0.01)
            link.command(f'dump_image {{{raw.as_posix()}}} {syms["display_frame"]:#x} {240*180*2}')
            link.command(f'dump_image {{{digit.as_posix()}}} {syms["g_digit_input"]:#x} 784')
        finally:
            link.command(f'write_memory {syms["g_command"]:#x} 32 {{4}}')
            time.sleep(0.2)
    values=np.frombuffer(raw.read_bytes(),dtype='<u2').reshape(180,240)
    rgb=np.stack([((values>>11)&31)*255//31,((values>>5)&63)*255//63,(values&31)*255//31],axis=-1).astype(np.uint8)
    Image.fromarray(rgb).save(str(args.prefix)+'_camera.png')
    Image.frombytes('L',(28,28),digit.read_bytes()).resize((168,168),Image.Resampling.NEAREST).save(str(args.prefix)+'_input.png')
    Path(str(args.prefix)+'_status.json').write_text(json.dumps(status,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(status),flush=True)

if __name__=='__main__':main()
