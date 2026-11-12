"""下载并校验 TensorFlow/Keras 托管的 MNIST，不依赖 PyTorch。"""
import hashlib
import urllib.request
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
DATA_URL='https://storage.googleapis.com/tensorflow/tf-keras-datasets/mnist.npz'
DATA_SHA256='731c5ac602752760c8e48fbffcf8c3b850d9dc2a2aedcf2cc48468fc17b673d1'

def dataset():
    path=ROOT/'data/mnist.npz';path.parent.mkdir(exist_ok=True)
    if not path.exists():
        print('Downloading official MNIST...',flush=True)
        temporary=path.with_suffix('.part');urllib.request.urlretrieve(DATA_URL,temporary)
        if hashlib.sha256(temporary.read_bytes()).hexdigest()!=DATA_SHA256:raise RuntimeError('MNIST checksum mismatch')
        temporary.replace(path)
    if hashlib.sha256(path.read_bytes()).hexdigest()!=DATA_SHA256:raise RuntimeError('MNIST checksum mismatch')
    with np.load(path,allow_pickle=False) as data:return {key:data[key].copy() for key in data.files}
