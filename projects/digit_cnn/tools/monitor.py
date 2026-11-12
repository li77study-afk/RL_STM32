"""只读 SWD 快照：相机状态、模型自检、识别结果及各阶段实测耗时。"""
import argparse
import json
import time
from pathlib import Path
from stlink import ROOT,STLink,symbols

FIELDS=('sequence magic version stage fatal_count heartbeat_ms sdram_ok camera_id captured displayed dropped copy_races '
        'dma_errors dcmi_errors spi_errors preview_fps_x100 copy_us display_us inference_count inference_us preprocess_us '
        'raw_digit confidence_permille recognized_digit preprocess_status threshold contrast foreground bbox_x bbox_y bbox_w bbox_h '
        'selftest_passed selftest_total selftest_max_error_micro input_mode processed_requests model_parameters model_macs core_hz frame_frozen').split()

def sample(link,address):
    for _ in range(100):
        first=link.words(address,1)[0]
        if first&1:continue
        values=link.words(address,len(FIELDS));last=link.words(address,1)[0]
        if first==values[0]==last and not last&1:
            row=dict(zip(FIELDS,values))
            if row['magic']!=0x434e4e31 or row['version']!=1:raise RuntimeError('Not matching Digit CNN firmware')
            return row
    raise RuntimeError('No coherent status snapshot')

def wait_ready(link,address,timeout=20):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        try:
            row=sample(link,address)
            if row['fatal_count']:raise RuntimeError('Board reported a fatal error')
            if row['stage']==7:return row
        except RuntimeError as error:
            if 'fatal' in str(error):raise
        time.sleep(0.1)
    raise TimeoutError('CNN application did not enter camera stage 7')

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--seconds',type=float,default=10)
    parser.add_argument('--output',type=Path,default=ROOT/'logs/monitor.json');args=parser.parse_args()
    syms=symbols();rows=[]
    with STLink() as link:
        wait_ready(link,syms['g_status']);start=time.monotonic()
        while True:
            row=sample(link,syms['g_status']);row['elapsed_s']=round(time.monotonic()-start,3);rows.append(row)
            print(json.dumps(row),flush=True)
            if any(row[key] for key in ['fatal_count','dma_errors','dcmi_errors','spi_errors']):raise RuntimeError('Board error in diagnostics')
            if row['elapsed_s']>=args.seconds:break
            time.sleep(2)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(dict(samples=rows,final=rows[-1]),indent=2)+'\n',encoding='utf-8')

if __name__=='__main__':main()
