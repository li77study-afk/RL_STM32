"""编译真实 C 算子为本机 DLL，评估全部 MNIST 测试图和合成相机预处理。

本测试执行固件所用 C 源码，不使用 Python 重新写一份推理公式来代替它。
合成相机测试只验证工程管线，不代表真实镜头、光照和纸面上的识别率。
"""
import argparse
import ctypes as C
import json
import subprocess
import time
from pathlib import Path
import numpy as np
from PIL import Image
from mnist_data import ROOT,dataset

class Result(C.Structure):
    _fields_=[('logits',C.c_float*10),('probabilities',C.c_float*10),('digit',C.c_uint32),('confidence',C.c_float)]
class PreInfo(C.Structure):
    _fields_=[(name,C.c_uint32) for name in 'status threshold contrast foreground bbox_x bbox_y bbox_w bbox_h'.split()]

def load_library():
    out=ROOT/'build-host';out.mkdir(exist_ok=True)
    compiler='D:/GOWIN/tools/llvm-mingw-20260616-ucrt-x86_64/bin/clang.exe'
    library=out/'digit_native.dll'
    subprocess.run([compiler,'-std=c11','-O2','-Wall','-Wextra','-Werror','-shared',
                    '-I'+str(ROOT/'ML'),*[str(ROOT/'ML'/source) for source in
                    ['digit_cnn.c','digit_model.c','digit_test_data.c','digit_preprocess.c']],'-o',str(library)],check=True)
    lib=C.CDLL(str(library))
    lib.DigitCNN_Run.argtypes=[C.POINTER(C.c_uint8),C.POINTER(Result)]
    lib.DigitCNN_SelfTest.argtypes=[C.POINTER(C.c_float)];lib.DigitCNN_SelfTest.restype=C.c_uint32
    lib.DigitPreprocess_RGB565.argtypes=[C.POINTER(C.c_uint16),C.c_uint32,C.c_uint32,C.POINTER(C.c_uint8),C.POINTER(PreInfo)]
    lib.DigitPreprocess_RGB565.restype=C.c_int
    return lib

def rgb565(gray):
    value=np.asarray(gray,dtype=np.uint16)
    return np.ascontiguousarray(((value>>3)<<11)|((value>>2)<<5)|(value>>3),dtype=np.uint16)

def camera_frame(image,side=96):
    # 把亮笔画黑底 MNIST 反色为白纸黑字，放到 240×180 中央框。
    scaled=np.asarray(Image.fromarray(image).resize((side,side),Image.Resampling.BILINEAR))
    frame=np.full((180,240),255,dtype=np.uint8)
    x,y=56+(128-side)//2,26+(128-side)//2
    frame[y:y+side,x:x+side]=255-scaled
    return rgb565(frame)

def preprocess(lib,frame):
    image=np.zeros((28,28),dtype=np.uint8);info=PreInfo()
    valid=lib.DigitPreprocess_RGB565(frame.ctypes.data_as(C.POINTER(C.c_uint16)),240,180,
                                    image.ctypes.data_as(C.POINTER(C.c_uint8)),C.byref(info))
    return valid,image,info

def predict(lib,image):
    result=Result();lib.DigitCNN_Run(image.ctypes.data_as(C.POINTER(C.c_uint8)),C.byref(result));return result

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--synthetic-count',type=int,default=1000);args=parser.parse_args()
    lib=load_library();error=C.c_float();passed=lib.DigitCNN_SelfTest(C.byref(error))
    assert passed==10, f'Selftest {passed}/10, error {error.value}'
    # 白、黑和均匀灰色都应被低对比度检查拒识，不能送入分类器硬猜一个类别。
    for level in [0,80,180,255]:
        valid,image,info=preprocess(lib,rgb565(np.full((180,240),level,dtype=np.uint8)))
        assert not valid and not image.any() and info.status==1
    # 两个像素的噪点不构成数字；触碰框边的大片笔画应提示移回取景框。
    noise=np.full((180,240),255,dtype=np.uint8);noise[80:82,100]=0
    valid,image,info=preprocess(lib,rgb565(noise));assert not valid and not image.any()
    edge=np.full((180,240),255,dtype=np.uint8);edge[26:120,70:80]=0
    valid,image,info=preprocess(lib,rgb565(edge));assert not valid and info.status==4
    data=dataset();correct=0;agreement=0;worst=0.0;start=time.monotonic();confusion=np.zeros((10,10),dtype=int)
    reference_path=ROOT/'models/reference.npz'
    reference=np.load(reference_path,allow_pickle=False) if reference_path.exists() else None
    for n,(image,label) in enumerate(zip(data['x_test'],data['y_test'])):
        result=predict(lib,image);correct+=result.digit==int(label);confusion[int(label),result.digit]+=1
        assert np.isfinite(list(result.logits)).all() and abs(sum(result.probabilities)-1)<1e-5
        if reference is not None:
            ref=reference['logits'][n];agreement+=result.digit==int(ref.argmax())
            worst=max(worst,float(np.abs(np.asarray(result.logits)-ref).max()))
    if reference is not None:
        assert agreement==10000 and worst<0.001,(agreement,worst)
    assert correct>=9750, f'MNIST accuracy too low: {correct}/10000'
    synthetic_correct=0;synthetic_valid=0;samples=[]
    for n in range(args.synthetic_count):
        valid,image,info=preprocess(lib,camera_frame(data['x_test'][n]))
        synthetic_valid+=bool(valid)
        if valid:
            result=predict(lib,image);synthetic_correct+=result.digit==int(data['y_test'][n])
        if n<10:samples.append(image)
    # 这项包含定位失败，不能通过只统计成功预处理的样本来抬高准确率。
    synthetic_accuracy=synthetic_correct/args.synthetic_count
    assert synthetic_accuracy>=0.90, f'Synthetic camera accuracy {synthetic_accuracy}'
    report=dict(selftest_passed=passed,selftest_max_logit_error=error.value,mnist_samples=10000,
                mnist_correct=correct,mnist_accuracy=correct/10000,python_prediction_agreement=agreement if reference is not None else None,
                maximum_logit_error=worst if reference is not None else None,synthetic_samples=args.synthetic_count,
                synthetic_preprocess_valid=synthetic_valid,synthetic_correct=synthetic_correct,synthetic_accuracy=synthetic_accuracy,
                confusion_matrix=confusion.tolist(),seconds=round(time.monotonic()-start,3))
    (ROOT/'docs/native_validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    preview=Image.new('L',(28*10,28))
    for n,image in enumerate(samples):preview.paste(Image.fromarray(image),(28*n,0))
    preview.resize((560,56),Image.Resampling.NEAREST).save(ROOT/'docs/preprocess_examples.png')
    print(json.dumps(report),flush=True)

if __name__=='__main__':main()
