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
from tqdm import tqdm

import sys

path = sys.argv[1]
BATCH_SIZE = 64

# HPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHP
_denseSize1 = 4096
_denseSize2 = 2048
_lr = 0.00001
# HPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHP
if torch.cuda.is_available():
    print("torch.cuda.is_available()")

device = torch.device("cuda:0" if torch.cuda.is_available() else "cpu")
tag = sys.argv[1][sys.argv[1].find('_') + 1:sys.argv[1].rfind('_')]

import os

if not os.path.exists('model'):
    os.system('mkdir model')
if not os.path.exists('preprocessed_data'):
    os.system('mkdir preprocessed_data')

# fileprefix = "model/model_{}_{}_{}_{}_partial_uncompress".format(tag, _denseSize1, _denseSize2, _lr)
fileprefix = "model/model_0_1_100_{}_{}_{}_{}".format(tag, _denseSize1, _denseSize2, _lr)
if os.path.exists(fileprefix + ".torchsave"):
    print("Skip. Already trained Model: " + fileprefix + ".torchsave")
    sys.exit()

logfile = open(fileprefix + ".log", 'w')

def log(logstr):
    print(logstr)
    logfile.write(logstr + '\n')

# Read data
from glob import glob
original_data = glob(os.path.join(path, '*/*'))
log("Original data:" + str(len(original_data)))

print(f'len original_data: {len(original_data)}')

numCluster = len(original_data) // 3

# numCluster = 602

print(f'numClustring: {numCluster}')

train_data = []
test_data = []

random.seed(1)
for i in range(numCluster):
    name = glob(os.path.join(path,'{}/*'.format(i)))
    random.shuffle(name)
    n = len(name)
    print(f'n: {n}')
    
    train_data += name[:n // 2]
    test_data += name[n // 2:]

log("Train data: " + str(len(train_data)))
log("Test data: " + str(len(test_data)))


def files_to_hash(files, sampling):
    m = hashlib.sha256()
    m.update(str(sampling).encode())
    for fn in sorted(files):
        m.update(fn.encode())

    return m.hexdigest()[:16] # Use the first 16 chars. It's too long

# Use it for large datasets
class RuntimeLoader:
    def __init__(self, files):
        self.dataset = files.copy()
        random.seed(1)
        random.shuffle(self.dataset)
        self.num = 0

    def __iter__(self):
        return self
    
    def __next__(self):
        num = self.num
        nowlen = min(len(self.dataset) - num, BATCH_SIZE)
        if nowlen > 0:
            self.num += nowlen
            ret1 = [[] for i in range(nowlen)]
            ret2 = []
            for i in range(nowlen):
                with open(self.dataset[num + i], 'rb') as f:
                    data = f.read()
                data = [int(d) for d in data]
                ret1[i] = data
                ret2.append(int(self.dataset[num + i][self.dataset[num + i].rfind('/') + 1:self.dataset[num + i].rfind('_')]))
            ret = [(torch.tensor(ret1) - 128) / 128.0, torch.tensor(ret2)]

            # for i in range(len(ret)):
            #     ret[i] = ret[i].cuda()
            return ret
        else:
            raise StopIteration

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
           
train_data_tensor = list(Loader(train_data, 1.0))

#test_data_tensor = list(RuntimeLoader(test_data))

log("Tensor conversion done")

def test(model, test_loader, epoch, print_progress=False):
    model.eval()
    test_loss = 0
    correct_1 = 0
    correct_5 = 0
    with torch.no_grad():
        total = 0
        cnt = 0
        for data, target in test_loader:
            data = data.to(device)
            target = target.to(device)
            output = model(data)
            prob, label = output.topk(5, 1, True, True)
            
            expanded = target.view(target.size(0), -1).expand_as(label)
            compare = label.eq(expanded).float()
            
            total += len(data)
            correct_1 += int(compare[:,:1].sum())
            correct_5 += int(compare[:,:5].sum())

            cnt += 1
            if print_progress and (cnt % 1000 == 0):
                log('Test Epoch: {}, Top 1 accuracy: {}/{} ({:.2f}%), Top 5 accuracy: {}/{} ({:.2f}%)'.format(epoch, correct_1, total, 100. * correct_1 / total, correct_5, total, 100. * correct_5 / total))

    log('Test Epoch: {}, Top 1 accuracy: {}/{} ({:.2f}%), Top 5 accuracy: {}/{} ({:.2f}%)'.format(
                epoch, correct_1, total, 100. * correct_1 / total,
                correct_5, total, 100. * correct_5 / total))
    return (test_loss / total, correct_1, correct_5)


def do_test(sampling, print_progress=False):
    if sampling == 1.0:
        it = RuntimeLoader(test_data)
    else:
        it = Loader(test_data, sampling)

    test(hidden_model, it, epoch, print_progress)

def do_eval(sampling):
    it = Loader(train_data, sampling)
    test(hidden_model, it, epoch)

class RevisedNetwork(torch.nn.Module):
    def __init__(self):
        super(RevisedNetwork, self).__init__()

        self.conv_layers = []
        self.layers = []

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
        
        # We add
        self.conv_layers.append(nn.Conv1d(in_channels=32, out_channels=32, kernel_size=3, stride=1, padding=1, bias=True))
        self.conv_layers.append(nn.ReLU()) 
        self.conv_layers.append(nn.BatchNorm1d(32))
        self.conv_layers.append(nn.MaxPool1d(2)) 
        
        self.conv_layers.append(nn.Conv1d(in_channels=32, out_channels=32, kernel_size=3, stride=1, padding=1, bias=True))
        self.conv_layers.append(nn.ReLU()) 
        self.conv_layers.append(nn.BatchNorm1d(32))
        self.conv_layers.append(nn.MaxPool1d(2)) 
        
        self.conv_layers.append(nn.Conv1d(in_channels=32, out_channels=32, kernel_size=3, stride=1, padding=1, bias=True))
        self.conv_layers.append(nn.ReLU()) 
        self.conv_layers.append(nn.BatchNorm1d(32))
        self.conv_layers.append(nn.MaxPool1d(2)) 
        
        self.conv_layers.append(nn.Conv1d(in_channels=32, out_channels=32, kernel_size=3, stride=1, padding=1, bias=True))
        self.conv_layers.append(nn.ReLU()) 
        self.conv_layers.append(nn.BatchNorm1d(32))
        self.conv_layers.append(nn.MaxPool1d(2)) 

        self.layers.append(nn.Linear(512*256, _denseSize1))
        self.layers.append(nn.ReLU()) 
        self.layers.append(nn.Dropout(p=0.5))


        last_denseSize = _denseSize1
        if _denseSize2 > 0:
            self.layers.append(nn.Linear(_denseSize1, _denseSize2))
            self.layers.append(nn.ReLU()) 
            self.layers.append(nn.Dropout(p=0.5))
            last_denseSize = _denseSize2

        self.fc = nn.Linear(last_denseSize, numCluster, bias=False)

        self.conv_layers = nn.ModuleList(self.conv_layers)
        self.layers = nn.ModuleList(self.layers)



    def forward(self, x):
        x = x.unsqueeze(dim=1)
        for l in self.conv_layers:
            x = l(x)

        x = x.view(x.shape[0], -1)
        for l in self.layers:
            x = l(x)
        x = self.fc(x)
        output = F.log_softmax(x, dim=1)
        return output
        

hidden_model = RevisedNetwork()

# # 加载现有模型
# hidden_model.load_state_dict(torch.load('./model/model__4096_2048_1e-05.torchsave',map_location=torch.device('cuda:0')))

hidden_model= hidden_model.to(device)
optimizer = optim.Adam(hidden_model.parameters(), lr = _lr, weight_decay=1e-4)

loss = []
prevtime = time.time()
prevloss = []
for epoch in range(1, 300):
    train_loss = 0
    for batch_idx, (data, target) in enumerate(train_data_tensor):
        data = data.to(device)
        target = target.to(device)
        optimizer.zero_grad()
        outputs = hidden_model(data)
        loss = F.nll_loss(outputs, target)
        train_loss += loss.item()
        loss.backward()
        optimizer.step()

    train_loss = train_loss / len(train_data_tensor)
    log('Epoch: {}\tLoss: {:.6f}\t{}'.format(
        epoch, train_loss, time.time()-prevtime))
    prevloss.append(train_loss)
    if len(prevloss) >= 10:
        mx = max(prevloss[-10:])
        mi = min(prevloss[-10:])
        if (mx - mi) / mi < 0.05:
            break

    prevtime = time.time()

    if epoch % 10 == 0:
        do_test(0.01)
        do_eval(0.1)
        torch.save(hidden_model.state_dict(), fileprefix + ".cp.torchsave")

torch.save(hidden_model.state_dict(), fileprefix + ".torchsave")
do_test(1.0, True)
logfile.close()
