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

import sys, os

BATCH_SIZE = 128 

basemodel_filename = sys.argv[1] # HPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHP
bn = os.path.basename(basemodel_filename)
bn = bn.replace("model_", "")
bn = bn.replace(".torchsave", "")
modelinfo = bn.split('_')
# if len(modelinfo) != 4:
#     print("Fail to extract model info!")
#     sys.exit()
tag = modelinfo[0]
_denseSize1 = int(modelinfo[1])
_denseSize2 = int(modelinfo[2])    

_hashSize = int(sys.argv[3])      # HPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHP
_which_dense = int(sys.argv[4])   # 1 or 2 HPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHP


device = torch.device("cuda:1" if torch.cuda.is_available() else "cpu")

################################################################################33
# Important parameters for greedy hash

_lr = 0.005
_alpha = 1
_epoch_lr_decrease = 250 #100

_lr = float(sys.argv[5])    # HPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHP
_alpha = float(sys.argv[6]) # HPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHPHP

print(_lr, _alpha)

def compute_result(dataloader, net):
    bs, clses = [], []
    net.eval()
    for img, cls in tqdm(dataloader):
        clses.append(F.one_hot(cls, num_classes=numCluster).cpu())
        _, __, code = net(img.to(device))
        bs.append(code.data.cpu())
    return torch.cat(bs), torch.cat(clses)

def CalcHammingDist(B1, B2):
    q = B2.shape[1]
    distH = 0.5 * (q - np.dot(B1, B2.transpose()))
    return distH

def CalcTopMap(rB, qB, retrievalL, queryL, topk=5):
    num_query = queryL.shape[0]
    topkmap = 0
    for it in tqdm(range(num_query)):
        gnd = (np.dot(queryL[it, :], retrievalL.transpose()) > 0).astype(np.float32)
        hamm = CalcHammingDist(qB[it, :], rB)
        ind = np.argsort(hamm)
        gnd = gnd[ind]

        tgnd = gnd[0:topk]
        tsum = np.sum(tgnd).astype(int)
        if tsum == 0:
            continue
        count = np.linspace(1, tsum, tsum)

        tindex = np.asarray(np.where(tgnd == 1)) + 1.0
        topkmap_ = np.mean(count / (tindex))
        topkmap = topkmap + topkmap_
    topkmap = topkmap / num_query
    return topkmap

class GreedyHashLoss(torch.nn.Module):
    def __init__(self, bit, alpha=1):
        super(GreedyHashLoss, self).__init__()
        self.fc = torch.nn.Linear(bit, numCluster, bias=False).to(device)
        self.criterion = torch.nn.CrossEntropyLoss().to(device)
        self.alpha = alpha

    def forward(self, outputs, y, feature):
#        print(outputs)
#       print(y)
#       print(feature)
        loss1 = self.criterion(outputs, y)
        loss2 = self.alpha * (feature.abs() - 1).pow(3).abs().mean()
#       print(feature.abs() - 1)
#       print(loss1)
#print(loss2)
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
################################################################################33


import os
# fileprefix = "model/model_hash_{}_{}_{}_{}_{}_{}_partial_uncompress".format(tag, _hashSize, _denseSize1, _denseSize2, _which_dense, _lr, _alpha)
fileprefix = "model/model_hash_{}_{}_{}_{}_{}_{}".format(tag, _hashSize, _denseSize1, _denseSize2, _which_dense, _lr, _alpha)
if os.path.exists(fileprefix + ".torchsave"):
    print("Skip. Already trained Model: " + fileprefix + ".torchsave")
    sys.exit()
print(fileprefix)

logfile = open(fileprefix + ".log", 'w')
def log(logstr):
    print(logstr)
    logfile.write(logstr + '\n')

# Read data
from glob import glob
path = sys.argv[2]

original_data = glob(os.path.join(path, '*/*'))
log("Original data:" + str(len(original_data)))

numCluster = len(original_data) // 3

train_data = []
test_data = []

random.seed(1)
for i in range(numCluster):
    name = glob(os.path.join(path,'{}/*'.format(i)))
    random.shuffle(name)
    n = len(name)
    
    train_data += name[:n // 2]
    test_data += name[n // 2:]

log("Train data:" + str(len(train_data)))
log("Test data:" + str(len(test_data)))

def files_to_hash(files, sampling):
    m = hashlib.sha256()
    m.update(str(sampling).encode())
    for fn in sorted(files):
        m.update(fn.encode())

    return m.hexdigest()[:16] # Use the first 16 chars. It's too longj


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

def compute_correct(outputs, targets): # for each batch
    prob, label = outputs.topk(5, 1, True, True)
    expanded = target.view(targets.size(0), -1).expand_as(label)
    compare = label.eq(expanded).float()

    correct_1 = int(compare[:,:1].sum())
    correct_5 = int(compare[:,:5].sum())
    return correct_1, correct_5

def train(model, train_loader, optimizer, epoch):
    model.train()
    for batch_idx, (data, target) in enumerate(train_loader):
        optimizer.zero_grad()
        output = model(data)
        loss = F.nll_loss(output, target)
        loss.backward()
        optimizer.step()
        #if epoch % 10 == 0 and batch_idx % 100 == 0:
        if batch_idx == 0:
            if train.prevtime == 0:
                log('Epoch: {}\tLoss: {:.6f}'.format(epoch, loss.item()))
            else:
                log('Epoch: {}\tLoss: {:.6f}\t{}'.format(epoch, loss.item(), time.time()-train.prevtime))
            train.prevtime = time.time()
train.prevtime = 0

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
            output, _, _ = model(data)
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

# class RevisedNetwork(torch.nn.Module):
#     def __init__(self):
#         super(RevisedNetwork, self).__init__()

#         self.conv_layers = []
#         self.layers = []

#         self.conv_layers.append(nn.Conv1d(in_channels=1, out_channels=8, kernel_size=3, stride=1, padding=1, bias=True)) 
#         self.conv_layers.append(nn.ReLU()) 
#         self.conv_layers.append(nn.BatchNorm1d(8))
#         self.conv_layers.append(nn.MaxPool1d(2)) 

#         self.conv_layers.append(nn.Conv1d(in_channels=8, out_channels=16, kernel_size=3, stride=1, padding=1, bias=True))
#         self.conv_layers.append(nn.ReLU()) 
#         self.conv_layers.append(nn.BatchNorm1d(16))
#         self.conv_layers.append(nn.MaxPool1d(2)) 

#         self.conv_layers.append(nn.Conv1d(in_channels=16, out_channels=32, kernel_size=3, stride=1, padding=1, bias=True))
#         self.conv_layers.append(nn.ReLU()) 
#         self.conv_layers.append(nn.BatchNorm1d(32))
#         self.conv_layers.append(nn.MaxPool1d(2)) 
        
#         # We add
#         self.conv_layers.append(nn.Conv1d(in_channels=32, out_channels=32, kernel_size=3, stride=1, padding=1, bias=True))
#         self.conv_layers.append(nn.ReLU()) 
#         self.conv_layers.append(nn.BatchNorm1d(32))
#         self.conv_layers.append(nn.MaxPool1d(2)) 
        
#         self.conv_layers.append(nn.Conv1d(in_channels=32, out_channels=32, kernel_size=3, stride=1, padding=1, bias=True))
#         self.conv_layers.append(nn.ReLU()) 
#         self.conv_layers.append(nn.BatchNorm1d(32))
#         self.conv_layers.append(nn.MaxPool1d(2)) 
        
#         self.conv_layers.append(nn.Conv1d(in_channels=32, out_channels=32, kernel_size=3, stride=1, padding=1, bias=True))
#         self.conv_layers.append(nn.ReLU()) 
#         self.conv_layers.append(nn.BatchNorm1d(32))
#         self.conv_layers.append(nn.MaxPool1d(2)) 
        
#         self.conv_layers.append(nn.Conv1d(in_channels=32, out_channels=32, kernel_size=3, stride=1, padding=1, bias=True))
#         self.conv_layers.append(nn.ReLU()) 
#         self.conv_layers.append(nn.BatchNorm1d(32))
#         self.conv_layers.append(nn.MaxPool1d(2)) 
        
#         self.conv_layers.append(nn.Conv1d(in_channels=32, out_channels=32, kernel_size=3, stride=1, padding=1, bias=True))
#         self.conv_layers.append(nn.ReLU()) 
#         self.conv_layers.append(nn.BatchNorm1d(32))
#         self.conv_layers.append(nn.MaxPool1d(2)) 
        
#         self.conv_layers.append(nn.Conv1d(in_channels=32, out_channels=32, kernel_size=3, stride=1, padding=1, bias=True))
#         self.conv_layers.append(nn.ReLU()) 
#         self.conv_layers.append(nn.BatchNorm1d(32))
#         self.conv_layers.append(nn.MaxPool1d(2)) 
        
#         self.conv_layers.append(nn.Conv1d(in_channels=32, out_channels=32, kernel_size=3, stride=1, padding=1, bias=True))
#         self.conv_layers.append(nn.ReLU()) 
#         self.conv_layers.append(nn.BatchNorm1d(32))
#         self.conv_layers.append(nn.MaxPool1d(2)) 
     
#         # # self.conv_layers.append(nn.Conv1d(in_channels=32, out_channels=64, kernel_size=3, stride=1, padding=1, bias=True))
#         # # self.conv_layers.append(nn.ReLU()) 
#         # # self.conv_layers.append(nn.BatchNorm1d(64))
#         # # self.conv_layers.append(nn.MaxPool1d(2)) 
        
#         # # self.conv_layers.append(nn.Conv1d(in_channels=64, out_channels=128, kernel_size=3, stride=1, padding=1, bias=True))
#         # # self.conv_layers.append(nn.ReLU()) 
#         # # self.conv_layers.append(nn.BatchNorm1d(128))
#         # # self.conv_layers.append(nn.MaxPool1d(2)) 

#         self.layers.append(nn.Linear(512*32, _denseSize1))
#         self.layers.append(nn.ReLU()) 
#         self.layers.append(nn.Dropout(p=0.5))
#         # self.layers.append(nn.Linear(4096 * 4, _denseSize1))
#         # self.layers.append(nn.ReLU()) 
#         # self.layers.append(nn.Dropout(p=0.5))


#         last_denseSize = _denseSize1
#         if _denseSize2 > 0:
#             self.layers.append(nn.Linear(_denseSize1, _denseSize2))
#             self.layers.append(nn.ReLU()) 
#             self.layers.append(nn.Dropout(p=0.5))
#             last_denseSize = _denseSize2

#         self.fc = nn.Linear(last_denseSize, numCluster, bias=False)

#         self.conv_layers = nn.ModuleList(self.conv_layers)
#         self.layers = nn.ModuleList(self.layers)

#     def forward(self, x):
#         x = x.unsqueeze(dim=1)
#         for l in self.conv_layers:
#             x = l(x)

#         x = x.view(x.shape[0], -1)
#         for l in self.layers:
#             x = l(x)
#         x = self.fc(x)
#         output = F.log_softmax(x, dim=1)
#         return output
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
        self.conv_layers.append(nn.Conv1d(in_channels=32, out_channels=16, kernel_size=3, stride=1, padding=1, bias=True))
        self.conv_layers.append(nn.ReLU()) 
        self.conv_layers.append(nn.BatchNorm1d(16))
        self.conv_layers.append(nn.MaxPool1d(4)) 
        
        self.conv_layers.append(nn.Conv1d(in_channels=16, out_channels=8, kernel_size=3, stride=1, padding=1, bias=True))
        self.conv_layers.append(nn.ReLU()) 
        self.conv_layers.append(nn.BatchNorm1d(8))
        self.conv_layers.append(nn.MaxPool1d(4)) 
        
        self.conv_layers.append(nn.Conv1d(in_channels=8, out_channels=8, kernel_size=3, stride=1, padding=1, bias=True))
        self.conv_layers.append(nn.ReLU()) 
        self.conv_layers.append(nn.BatchNorm1d(8))
        self.conv_layers.append(nn.MaxPool1d(8)) 
        
        
        
        
     
        # # self.conv_layers.append(nn.Conv1d(in_channels=32, out_channels=64, kernel_size=3, stride=1, padding=1, bias=True))
        # # self.conv_layers.append(nn.ReLU()) 
        # # self.conv_layers.append(nn.BatchNorm1d(64))
        # # self.conv_layers.append(nn.MaxPool1d(2)) 
        
        # # self.conv_layers.append(nn.Conv1d(in_channels=64, out_channels=128, kernel_size=3, stride=1, padding=1, bias=True))
        # # self.conv_layers.append(nn.ReLU()) 
        # # self.conv_layers.append(nn.BatchNorm1d(128))
        # # self.conv_layers.append(nn.MaxPool1d(2)) 

        self.layers.append(nn.Linear(512*8, _denseSize1))
        self.layers.append(nn.ReLU()) 
        self.layers.append(nn.Dropout(p=0.5))
        # self.layers.append(nn.Linear(4096 * 4, _denseSize1))
        # self.layers.append(nn.ReLU()) 
        # self.layers.append(nn.Dropout(p=0.5))


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


class HashNetwork(torch.nn.Module):
    def __init__(self, revNet):
        super(HashNetwork, self).__init__()

        self.conv_layers = []
        self.layers = []

        self.conv_layers = revNet.conv_layers
        self.layers = revNet.layers

        last_denseSize = _denseSize1

        if _denseSize2 > 0:
            if _which_dense == 2:
                last_denseSize = _denseSize2

        self.fc_plus = nn.Linear(last_denseSize, _hashSize)
        self.fc = nn.Linear(_hashSize, numCluster, bias=False)


    def forward(self, x):
        x = x.unsqueeze(dim=1)
        for l in self.conv_layers:
            x = l(x)

        x = x.view(x.shape[0], -1)

        if _which_dense == 2:
            for l in self.layers:
                x = l(x)
        else:
            x = self.layers[0](x)

        x = self.fc_plus(x)
        code = GreedyHashLoss.Hash.apply(x)
        output = self.fc(code)

        return output, x, code
        

# Load basemodel
print("Base Model: " + basemodel_filename)
basemodel = RevisedNetwork()
basemodel.load_state_dict(torch.load(basemodel_filename))

hidden_model = HashNetwork(basemodel)

# # 加载自己网络
# hidden_model.load_state_dict(torch.load('./model/model_hash__128_4096_2048_2_0.0001.torchsave'))

hidden_model= hidden_model.to(device)
#optimizer = optim.Adam(hidden_model.parameters(), lr = 0.00001) #weight_decay=0.001)
optimizer = optim.SGD(
        hidden_model.parameters(),
        lr = _lr,
        weight_decay = 5e-4,
        momentum = 0.9)
criterion = GreedyHashLoss(_hashSize, _alpha)

loss = []
prevtime = time.time()
prevloss = []

# for epoch in range(1, 351):
for epoch in range(1, 1000):
    #train(hidden_model, train_data_tensor, optimizer, epoch)
    lr = _lr * (0.1 ** (epoch // _epoch_lr_decrease))
    for param_group in optimizer.param_groups:
        param_group['lr'] = lr

    hidden_model.train()
    train_loss = 0
    #for image, label, ind in train_data_tensor:
    for batch_idx, (data, target) in enumerate(train_data_tensor):
        data = data.to(device)
        target = target.to(device)
        optimizer.zero_grad()
        outputs, feature, _ = hidden_model(data)

        loss = criterion(
                outputs,
                target,
                feature)
        train_loss += loss.item()

        loss.backward()
        optimizer.step()

    train_loss = train_loss / len(train_data_tensor)
    log('Epoch: {}\tLoss: {:.6f}\t{}'.format(
        epoch, train_loss, time.time()-prevtime))
    prevloss.append(train_loss)
#    if len(prevloss) >= 10:
#        for _ in range(1, 10):
#            if abs(prevloss[-_] - prevloss[-_ - 1]) / prevloss[-_] > 0.002:
#                break
#        else:
#            break
    prevtime = time.time()


    if epoch % 10 == 0:
        do_test(0.01)
        do_eval(0.1)
        torch.save(hidden_model.state_dict(), fileprefix + ".cp.torchsave")

#            if epoch % 100 == 0:
#                loss.append(test(hidden_model, test_data_tensor, epoch))


torch.save(hidden_model.state_dict(), fileprefix + ".torchsave")

#tst_binary, tst_label = compute_result(
#        small_tst_data_loader, hidden_model)
#trn_binary, trn_label = compute_result(
#        small_trn_data_loader, hidden_model)
#
#mAP = CalcTopMap(
#        trn_binary.numpy(), tst_binary.numpy(),
#        trn_label.numpy(), tst_label.numpy())
#
#log('Final mAP: {})'.format(mAP))

do_test(1.0, True)

logfile.close()
