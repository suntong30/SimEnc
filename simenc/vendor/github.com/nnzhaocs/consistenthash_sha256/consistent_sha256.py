import hashlib
import random
import binascii
import bisect

class consistent_sha256:

    def __init__(self, replicas, pseudoIDs):
        self.replicas = replicas
        self.pseudoIDs = pseudoIDs
        self.idmap = {}
        self.sortedIDs = []

    def bsearch(self, key):
        lower = 0
        upper = len(self.sortedIDs) - 1
        if upper == -1:
            return 0
        elif key <= self.sortedIDs[0] or key > self.sortedIDs[upper]:
                return 0
        elif key == self.sortedIDs[upper]:
            return upper
    
        while lower < upper:
            x = lower + (upper - lower) // 2
            if self.sortedIDs[x] == key:
                return x
            elif self.sortedIDs[x] > key:
                upper = x
            else:
                lower = x
            if lower == upper - 1:
                return upper
    

    def read_hash(self, key):
        index = self.bsearch(key)

        nodes = []
        i = 0
        while i < self.replicas:
            nodeID = self.sortedIDs[index]
            index = (index + 1) % len(self.sortedIDs)
            node = self.idmap[nodeID]
            if node in nodes:
                continue
            nodes.append(node)
            i += 1

        return random.choice(nodes)
        

    def write_hash(self, key):
        index = self.bsearch(key)
        return self.idmap[self.sortedIDs[index]]

    def hash_chain(self, seedID):
        IDs = [hashlib.sha256(seedID).digest()]

        for i in range(self.pseudoIDs - 1):
            IDs.append(hashlib.sha256(IDs[-1]).digest())

        return sorted(IDs)

    def add_node(self, ID):
        ids = self.hash_chain(ID)
        for i in ids:
            h = binascii.hexlify(i)
            bisect.insort(self.sortedIDs, h)
            self.idmap[h] = ID

    def del_node(self, seedID):
        ids = self.hash_chain(seedID)
        for i in ids:
            h = binascii.hexlify(i)
            self.idmap.pop(h, 0)
            self.sortedIDs.remove(h)


def print_stats(d, trials):
    for i in range(1, 6):
        print 'node' + str(i) + ': ' + str(1.*d['node' + str(i)] / trials)

    print ' '

def clear(d):
    for k in d:
        d[k] = 0
    return d

if __name__ == '__main__':

    a = consistent_sha256(3, 100)

    a.add_node('node1')
    a.add_node('node2')
    a.add_node('node3')
    a.add_node('node4')
    a.add_node('node5')
    d = {
        'node1': 0,
        'node2': 0,
        'node3': 0,
        'node4': 0,
        'node5': 0
    }

    for i in range(1000):
        node = a.write_hash(hashlib.sha256(str(i)).hexdigest())
        d[node] += 1
    print_stats(d, 1000)
    d = clear(d)
    for i in range(1000):
        node = a.read_hash(hashlib.sha256(str(i)).hexdigest())
        d[node] += 1
    print_stats(d, 1000)
    d = clear(d)
    
    node = a.read_hash(hashlib.sha256('a').hexdigest())
    a.del_node(node[1])
    for i in range(1000):
        node = a.write_hash(hashlib.sha256(str(i)).hexdigest())
        d[node] += 1
    print_stats(d, 1000)
    d = clear(d)
    for i in range(1000):
        node = a.write_hash(hashlib.sha256(str(i)).hexdigest())
        d[node] += 1
    print_stats(d, 1000)
