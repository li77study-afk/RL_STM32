"""训练 28×28 手写数字 CNN，并导出不依赖 Python 的 C 参数和自检样本。

电脑端使用 PyTorch 做监督训练；STM32 只使用导出的 float 数组和手写 C 算子。
用 MNIST 训练集划分训练/验证集，验证集选检查点，测试集只用于最终报告。
"""
import argparse
import hashlib
import json
import math
import random
import time

import numpy as np
import torch
from torch import nn
from torch.nn import functional as F

from mnist_data import ROOT, DATA_URL, DATA_SHA256, dataset

class DigitCNN(nn.Module):
    """CHW 布局，same padding；与 ML/digit_cnn.c 的循环逐项对应。"""
    def __init__(self):
        super().__init__()
        self.conv1 = nn.Conv2d(1, 8, 3, padding=1)
        self.conv2 = nn.Conv2d(8, 16, 3, padding=1)
        self.fc = nn.Linear(16*7*7, 10)

    def forward(self, x):
        x = F.max_pool2d(F.relu(self.conv1(x)), 2)
        x = F.max_pool2d(F.relu(self.conv2(x)), 2)
        return self.fc(x.flatten(1))

def augment(x):
    """小幅旋转/缩放/平移，减少模型对严格居中和固定笔画大小的依赖。"""
    n = len(x)
    angle = (torch.rand(n)-0.5)*(20*math.pi/180)
    scale = 0.90+torch.rand(n)*0.20
    affine = torch.zeros(n,2,3)
    affine[:,0,0] = scale*angle.cos(); affine[:,0,1] = -scale*angle.sin()
    affine[:,1,0] = scale*angle.sin(); affine[:,1,1] = scale*angle.cos()
    affine[:,:,2] = (torch.rand(n,2)-0.5)*(4.0/28)
    grid = F.affine_grid(affine, x.shape, align_corners=False)
    return F.grid_sample(x, grid, align_corners=False)

def evaluate(model, x, y, batch=512):
    model.eval()
    outputs = []
    with torch.no_grad():
        for offset in range(0,len(x),batch):
            outputs.append(model(x[offset:offset+batch]))
    logits = torch.cat(outputs)
    return float((logits.argmax(1)==y).float().mean()), logits

def c_array(name, array, c_type='float'):
    flat = np.asarray(array).ravel()
    if c_type == 'float':
        # 9 位有效数字足以往返表示 float32，保留 .0 防止产生无效的 1f 字面量。
        def literal(value):
            token = format(float(value), '.9g')
            if '.' not in token and 'e' not in token.lower(): token += '.0'
            return token+'f'
        values = [literal(value) for value in flat]
        width = 8
    else:
        values = [str(int(value)) for value in flat]
        width = 28
    lines = [f'const {c_type} {name}[{len(flat)}] = {{']
    lines += ['    '+', '.join(values[i:i+width])+',' for i in range(0,len(values),width)]
    return '\n'.join(lines+['};',''])

def export(model, data, report, logits):
    state = {key: value.detach().cpu().numpy() for key,value in model.state_dict().items()}
    out = ['/* 自动生成：tools/train.py。布局为 OIHW 卷积、OI 全连接。 */', '#include "digit_model.h"', '']
    for key, array in state.items():
        out.append(c_array('digit_'+key.replace('.','_'),array))
    (ROOT/'ML/digit_model.c').write_text('\n'.join(out),encoding='utf-8')
    # 每类固定选择一个高置信、预测正确的真实测试样本，用于板上开机自检。
    probabilities = torch.softmax(logits,1).numpy()
    predictions = logits.argmax(1).numpy()
    indices = []
    for label in range(10):
        eligible = np.flatnonzero((data['y_test']==label)&(predictions==label)&(probabilities[:,label]>0.98))
        if not len(eligible): raise RuntimeError(f'No correct high-confidence self-test sample for {label}')
        indices.append(int(eligible[0]))
    tests = ['/* 真实 MNIST 测试样本；只用于开机自检，不计入训练数据。 */', '#include "digit_model.h"', '']
    images = data['x_test'][indices]
    tests.append(c_array('digit_test_images',images,'uint8_t'))
    tests.append(c_array('digit_test_logits',logits.numpy()[indices]))
    (ROOT/'ML/digit_test_data.c').write_text('\n'.join(tests),encoding='utf-8')
    np.savez(ROOT/'models/reference.npz', images=data['x_test'], labels=data['y_test'], logits=logits.numpy(), **state)
    report['selftest_test_indices'] = indices
    report['export_sha256'] = hashlib.sha256((ROOT/'ML/digit_model.c').read_bytes()).hexdigest()
    (ROOT/'models/training_report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--epochs',type=int,default=12)
    parser.add_argument('--threads',type=int,default=4)
    parser.add_argument('--seed',type=int,default=750)
    args = parser.parse_args()
    random.seed(args.seed);np.random.seed(args.seed);torch.manual_seed(args.seed)
    torch.set_num_threads(args.threads)
    data = dataset()
    x = torch.from_numpy(data['x_train']).float().unsqueeze(1)/255
    y = torch.from_numpy(data['y_train'].astype(np.int64))
    order = torch.randperm(len(x));train_idx,val_idx = order[:55000],order[55000:]
    model = DigitCNN();optimizer = torch.optim.Adam(model.parameters(),lr=0.001)
    scheduler = torch.optim.lr_scheduler.StepLR(optimizer,step_size=8,gamma=0.3)
    history=[];best_accuracy=-1;start=time.monotonic()
    for epoch in range(args.epochs):
        model.train();epoch_start=time.monotonic();loss_sum=0
        perm=train_idx[torch.randperm(len(train_idx))]
        for offset in range(0,len(perm),256):
            idx=perm[offset:offset+256]
            optimizer.zero_grad(set_to_none=True)
            loss=F.cross_entropy(model(augment(x[idx])),y[idx])
            loss.backward();optimizer.step();loss_sum+=float(loss.detach())*len(idx)
        val_accuracy,_=evaluate(model,x[val_idx],y[val_idx]);scheduler.step()
        row=dict(epoch=epoch+1,train_loss=loss_sum/len(train_idx),validation_accuracy=val_accuracy,seconds=round(time.monotonic()-epoch_start,3))
        history.append(row);print(json.dumps(row),flush=True)
        if val_accuracy>best_accuracy:
            best_accuracy=val_accuracy
            torch.save(model.state_dict(),ROOT/'models/best.pt')
    model.load_state_dict(torch.load(ROOT/'models/best.pt',weights_only=True))
    test_x=torch.from_numpy(data['x_test']).float().unsqueeze(1)/255
    test_y=torch.from_numpy(data['y_test'].astype(np.int64))
    test_accuracy,logits=evaluate(model,test_x,test_y)
    confusion=np.zeros((10,10),dtype=int)
    for label,pred in zip(data['y_test'],logits.argmax(1).numpy()):confusion[int(label),int(pred)]+=1
    report=dict(seed=args.seed,torch_version=torch.__version__,dataset_url=DATA_URL,dataset_sha256=DATA_SHA256,
                train_samples=55000,validation_samples=5000,test_samples=10000,epochs=args.epochs,
                architecture='Conv3x3(1,8)-ReLU-MaxPool2-Conv3x3(8,16)-ReLU-MaxPool2-FC(784,10)',
                parameters=sum(p.numel() for p in model.parameters()),best_validation_accuracy=best_accuracy,
                test_accuracy=test_accuracy,confusion_matrix=confusion.tolist(),history=history,
                training_seconds=round(time.monotonic()-start,3))
    export(model,data,report,logits)
    print('FINAL',json.dumps({k:v for k,v in report.items() if k not in ['history','confusion_matrix']}),flush=True)

if __name__=='__main__':main()
