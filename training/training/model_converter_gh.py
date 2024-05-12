import torch
import torch.nn as nn
import torch.nn.functional as F
import torch.optim as optim
from torchvision import datasets, transforms
import random
import numpy as np

import hashlib
import pickle
import time

from glob import glob
import os, sys
from collections import defaultdict
from tqdm import tqdm

BATCH_SIZE = 128
# device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
device = torch.device('cpu')
numCluster = 20000 ## CHANGECHANGECHANGECHANGECHANGECHANGECHANGECHANGECHANGE
typeCluster = "large"

filename = sys.argv[1]
bn = os.path.basename(filename)
bn = bn.replace("model_hash_", "")
bn = bn.replace(".torchsave", "")
modelinfo = bn.split('_')
if len(modelinfo) != 6:
    print(modelinfo)
    print("Fail to extract model info!")
    sys.exit()
tag = modelinfo[0]
_hashSize = int(modelinfo[1])
_denseSize1 = int(modelinfo[2])
_denseSize2 = int(modelinfo[3])
_which_dense = int(modelinfo[4])

print(_hashSize)
print(_denseSize1)
print(_denseSize2)
print(_which_dense)

if len(sys.argv) >= 3:
    hashdict_filename = sys.argv[1] + "." + sys.argv[2]
else:
    hashdict_filename = "hashdict.txt"

#random.seed(1)
#torch.cuda.manual_seed(1)

# Read data
from glob import glob
path = sys.argv[2]

original_data = glob(os.path.join(path, '*/*'))

numCluster = len(original_data) // 3


class GreedyHashLoss(torch.nn.Module):
    def __init__(self, bit, alpha=1):
        super(GreedyHashLoss, self).__init__()
        self.fc = torch.nn.Linear(bit, numCluster, bias=False).to(device)
        self.criterion = torch.nn.CrossEntropyLoss().to(device)
        self.alpha = alpha

    def forward(self, outputs, y, feature):
        loss1 = self.criterion(outputs, y)
        loss2 = self.alpha * (feature.abs() - 1).pow(3).abs().mean()
        return loss1 + loss2

    class Hash(torch.autograd.Function):
        @staticmethod
        def forward(ctx, input):
            # ctx.save_for_backward(input)
            return input.sign()

        @staticmethod
        def backward(ctx, grad_output):
            # input,  = ctx.saved_tensors
            # grad_output = grad_output.data
            return grad_output

class RevisedNetwork(torch.nn.Module):
    def __init__(self):
        super(RevisedNetwork, self).__init__()

        self.conv_layers = []
        self.layers = []

        # The structure is copied from train_baseline.py
        self.conv_layers.append(nn.Conv1d(in_channels=1, out_channels=8, kernel_size=3, stride=1, padding=1, bias=True)) 
        self.conv_layers.append(nn.ReLU()) 
        self.conv_layers.append(nn.BatchNorm1d(8))
        self.conv_layers.append(nn.MaxPool1d(2)) 

        self.conv_layers.append(nn.Conv1d(in_channels=8, out_channels=16, kernel_size=3, stride=1, padding=1, bias=True))
        self.conv_layers.append(nn.ReLU()) 
        self.conv_layers.append(nn.BatchNorm1d(16))
        self.conv_layers.append(nn.MaxPool1d(2)) 

        self.conv_layers.append(nn.Conv1d(in_channels=16, out_channels=32, kernel_size=3, stride=1, padding=1, bias=True))
        self.conv_layers.append(nn.ReLU()) 
        self.conv_layers.append(nn.BatchNorm1d(32))
        self.conv_layers.append(nn.MaxPool1d(2)) 

        self.layers.append(nn.Linear(4096 * 4, _denseSize1))
        self.layers.append(nn.ReLU()) 
        self.layers.append(nn.Dropout(p=0.5))

        self.layers.append(nn.Linear(_denseSize1, _denseSize2))
        self.layers.append(nn.ReLU()) 
        self.layers.append(nn.Dropout(p=0.5))
        last_denseSize = _denseSize2

        self.conv_layers = nn.ModuleList(self.conv_layers)
        self.layers = nn.ModuleList(self.layers)

        self.fc_plus = nn.Linear(last_denseSize, _hashSize)
        self.fc = nn.Linear(_hashSize, numCluster, bias=False)


    def forward(self, x):
        x = x.unsqueeze(dim=1)
        for l in self.conv_layers:
            x = l(x)

        x = x.view(x.shape[0], -1)
#for l in self.layers:
#            x = l(x)
        x = self.layers[0](x)

        x = self.fc_plus(x)
        code = GreedyHashLoss.Hash.apply(x)
        output = self.fc(code)

        return output, x, code

print("Model Loading")
model = RevisedNetwork()
model.load_state_dict(torch.load(filename))
model.eval()

class InferNetwork(torch.nn.Module):
    def __init__(self):
        super(InferNetwork, self).__init__()
        self.conv_layers = None
        self.layers = None
        self.fc_plus = None
        
    def forward(self, x):
        x = x.unsqueeze(dim=1)
        for l in self.conv_layers:
            x = l(x)

        x = x.view(x.shape[0], -1)
        x = self.layer(x)

        x = self.fc_plus(x)
        x = x.sign()

        return x

# Save InferNet 
print("Model Saving to PT")
infer = InferNetwork()
infer.conv_layers = model.conv_layers
infer.layer = model.layers[0]
infer.fc_plus = model.fc_plus 
infer.eval()

torch.save(infer, './Mymodel.pt')

# file = open('./0_112596.bin', 'rb')
# data = file.read()
# file.close()
# print(data)
# data = [int(d)for d in data]
# data_tensor = torch.tensor(data, dtype=torch.float32)
# data_tensor = (data_tensor-128)/128.0
# data_tensor = data_tensor.unsqueeze(0)
# print(data_tensor)
# output = model(data_tensor)
# print(f'output: {output}')
sm = torch.jit.script(infer)
sm.save("{}.pt".format(filename))

#sys.exit()

# Evaluation ############################################################################
for l in infer.conv_layers:
    print(l)
print(infer.layer)

def files_to_hash(files, sampling):
    m = hashlib.sha256()
    m.update(str(sampling).encode())
    for fn in sorted(files):
        m.update(fn.encode())

    return m.hexdigest()[:16] # Use the first 16 chars. It's too longj


# Use it when the data is small enough to store in the main memory
class Loader:
    def __init__(self, files, sampling=1.0):
        picklename = "preprocessed_data/" + files_to_hash(files, sampling) + ".data"
        if os.path.exists(picklename):
            with open(picklename, 'rb') as f:
                self.alldata = pickle.load(f)
                self.alllen = self.alldata[2]
        else:
            self.dataset = files.copy()
            random.seed(1)
            random.shuffle(self.dataset)
            if sampling < 1.0:
                self.dataset = self.dataset[:int(len(self.dataset)*sampling)]
            self.alldata = self.load_data()
            self.alllen = self.alldata[2]
            with open(picklename, 'wb') as f:
                pickle.dump(self.alldata, f)
        
        self.num = 0

    def load_data(self):
        alllen = len(self.dataset)
        ret1 = [[] for i in range(alllen)]
        ret2 = []
        for i in range(alllen):
            with open(self.dataset[i], 'rb') as f:
                data = f.read()
            data = [int(d)for d in data]
            ret1[i] = data
            fn = self.dataset[i]
            ret2.append(int(fn[fn.rfind('/') + 1:fn.rfind('_')]))
        return ((torch.tensor(ret1)-128)/128.0), torch.tensor(ret2), alllen

    def __iter__(self):
        return self

    def __next__(self):
        num = self.num
        nowlen = min(self.alllen - num, BATCH_SIZE)
        
        if nowlen > 0:
            self.num += nowlen
            ret1 = self.alldata[0][num:num+nowlen]
            ret2 = self.alldata[1][num:num+nowlen]
            ret = [ret1, ret2]

            # for i in range(len(ret)):
            #     ret[i] = ret[i].cuda()
            return ret
        else:
            raise StopIteration



train_data = []
test_data = []

random.seed(1)
for i in range(numCluster):
    name = glob(os.path.join(path,'{}/*'.format(i)))

    random.shuffle(name)
    n = len(name)
    
    train_data += name[:n // 10]
    test_data += name[n // 10:]

print("Train data:", len(train_data))
print("Test data:", len(test_data))

# Evaluation 1: Training data validation
train_data_tensor = list(Loader(train_data))

print("Generating Hash Dict")
hashdict = defaultdict(list)
with torch.no_grad():
    for data, target in tqdm(train_data_tensor):
        out = infer.forward(data)
        tvecs = out.cpu().detach().numpy()
        for t, l in zip(tvecs, target):
            hval = ''.join(['1' if x>=0.0 else '0' for x in t])
            hashdict[int(l)].append(hval)
            

'''
def compute_score(hashdict):
    score = 0
    score_dict = dict()
    for k in hashdict.keys():
        n_hash = len(hashdict[k])
        max_score_per_cluster = 0
        for i in range(n_hash):
            cur_score = 0
            for j in range(n_hash):
                if i == j:
                    cur_score += 1
                    continue

                if hashdict[k][i] == hashdict[k][j]:
                    cur_score += 1

            if max_score_per_cluster < cur_score:
                max_score_per_cluster = cur_score

        score_dict[k] = max_score_per_cluster
        score += max_score_per_cluster

    return score, score_dict

score = None
score, score_dict = compute_score(hashdict)

with open(hashdict_filename, 'w') as f:
    total_size = 0
    for k in hashdict.keys():
        n_hash =  len(hashdict[k])
        total_size += n_hash

        f.write("Class\t{}\t{}\t{}\n".format(k, score_dict[k], n_hash))
        for hval in hashdict[k]:
            f.write("{}\n".format(hval))


    if score is not None:
        f.write("Total Score: {} / {}\n".format(score, total_size))
    

# Evalution 2: BLK data validation
sys.exit()
def read_blkfile_to_tensor(filename, n):
    ret = []
    with open(filename, 'rb') as f:
        for i in range(n):
            data = f.read(4096)
            data = [int(d) for d in data]
#print(', '.join(map(lambda x: str(x), data)))
            ret.append(data)
    return ((torch.tensor(ret)-128)/128.0)
 
N = 100
inp = read_blkfile_to_tensor("/home/compu/jeonggyun/ts/build/mix10", N)
#print(inp[0])
with torch.no_grad():
    out = infer.forward(inp.cuda())

for i in range(N):
#print(', '.join(map(lambda x: "%.2f" % x, inp[i].cpu().detach().numpy()[:10])))
    print(', '.join(map(lambda x: "%.6f" % x, out[i].cpu().detach().numpy()[:10])))
    t = [1 if x >=0.0 else 0 for x in t]
    print(''.join(map(lambda x: str(x), t)))
'''
