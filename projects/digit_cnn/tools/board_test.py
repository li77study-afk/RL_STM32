"""通过 SWD 发送已知 28×28 图像，验证真实 STM32 CNN；结束自动恢复相机模式。

此测试绕过摄像头预处理，只验证板上网络。与 live camera 测试分开报告。
"""
import argparse
import json
import time
import numpy as np
from mnist_data import dataset
from stlink import ROOT,STLink,symbols
from monitor import sample,wait_ready

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--count',type=int,default=100);args=parser.parse_args()
    data=dataset();syms=symbols();rows=[]
    reference_path=ROOT/'models/reference.npz'
    if reference_path.exists():
        with np.load(reference_path,allow_pickle=False) as reference:
            reference_predictions=reference['logits'].argmax(1)
    else:reference_predictions=None
    with STLink() as link:
        wait_ready(link,syms['g_status'])
        try:
            for n in range(args.count):
                before=sample(link,syms['g_status'])['processed_requests']
                image=data['x_test'][n].ravel()
                link.command(f'write_memory {syms["g_test_image"]:#x} 8 '+'{'+ ' '.join(str(int(x)) for x in image)+'}')
                link.command(f'write_memory {syms["g_command"]:#x} 32 {{1}}')
                deadline=time.monotonic()+5
                while True:
                    row=sample(link,syms['g_status'])
                    if row['processed_requests']>before:break
                    if time.monotonic()>deadline:raise TimeoutError('Board inference request timed out')
                    time.sleep(0.01)
                row={key:row[key] for key in ['raw_digit','confidence_permille','inference_us','selftest_passed','fatal_count','dma_errors','dcmi_errors','spi_errors']}
                row.update(index=n,label=int(data['y_test'][n]))
                if reference_predictions is not None:
                    row['reference_digit']=int(reference_predictions[n])
                    assert row['raw_digit']==row['reference_digit'],f'Board/reference mismatch at {n}'
                rows.append(row)
                if any(row[key] for key in ['fatal_count','dma_errors','dcmi_errors','spi_errors']):raise RuntimeError('Board error')
                if n%10==0:print(json.dumps(row),flush=True)
        finally:
            link.command(f'write_memory {syms["g_command"]:#x} 32 {{2}}')
            time.sleep(0.2)
    correct=sum(row['raw_digit']==row['label'] for row in rows)
    report=dict(count=len(rows),correct=correct,accuracy=correct/len(rows),
                reference_agreement=len(rows) if reference_predictions is not None else None,
                inference_us_min=min(row['inference_us'] for row in rows),
                inference_us_mean=float(np.mean([row['inference_us'] for row in rows])),
                inference_us_max=max(row['inference_us'] for row in rows),samples=rows)
    (ROOT/'docs/board_inference_validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('FINAL',json.dumps({key:value for key,value in report.items() if key!='samples'}),flush=True)

if __name__=='__main__':main()
