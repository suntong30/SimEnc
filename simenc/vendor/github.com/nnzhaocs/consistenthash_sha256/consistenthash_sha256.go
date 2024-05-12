package ConsistentHash_Sha256

import (
    "crypto/sha256"
    "sync"
    "encoding/hex"
    "fmt"
    "math/rand"
    "time"
)

//pseudoid type and helper functions
//pseudoids are 256 bits
const idsize = 32
const defaultReplicas = 3 //default value for number of replicas is 3
const defaultPsuedoIDs = 50 //default value for number of pseudoIDs per node is 100

type id [idsize]byte

// a < b
func (a * id) lessThan(b id) bool {
    for i, v := range a {
        if v < b[i] {
            return true
        } else if v > b[i] {
            return false
        }
    }
    return false
}

// a > b
func (a * id) greaterThan(b id) bool {
    for i, v := range a {
        if v > b[i] {
            return true
        } else if v < b[i] {
            return false
        }
    }
    return false
}

// a == b
func (a * id) equals(b id) bool {
    for i, v := range a {
        if b[i] != v {
            return false
        }
    }
    return true
}

func (i * id) toString() string {
    return fmt.Sprintf("%x", *i)
}

func fromString(seed string) (id, error) {
    var ret id
    s, err := hex.DecodeString(seed)
    if err != nil || len(s) != 32 {
        return ret, fmt.Errorf("Seed value not 64 hex characters")
    }
    copy(ret[:], s)
    return ret, nil
}

//id list type and helper functions
type idList []id


//Returns the index of the next value higher than the key if key is not present
//if key is greater than greatest value, returns 0 index
func (lp * idList) bsearch(key id) int {
    list := *lp
    lower := 0
    upper := len(list) - 1
    if upper == -1 {
        return 0
    }
    if key.greaterThan(list[upper]) || key.lessThan(list[lower]) || key.equals(list[lower]) {
        return 0
    } else if key.equals(list[upper]) {
        return upper
    }

    for lower < upper {
        x := lower + ((upper - lower) / 2)
        if key.equals(list[x]) {
            return x
        } else if key.greaterThan(list[x]) {
            lower = x
        } else {
            upper = x
        }
        if lower == upper - 1 {
            return upper
        }
    }
    return upper
}

//Inserts Value in sorted way
func (list * idList) insert(value id) {
    if len(*list) == 0 {
        *list = []id{value}
    } else {
        index := list.bsearch(value)
        if index == 0 {
            if value.lessThan((*list)[0]) {
                *list = append([]id{value}, (*list)...)
            } else {
            *list = append((*list), value)
            }
        } else {
            var tmp id
            *list = append((*list), tmp)
            copy((*list)[index + 1:], (*list)[index:])
            (*list)[index] = value
        }
    }
}

//removes value if present. Does noting if not present
func (list * idList) remove(value id) {
    index := list.bsearch(value)
    if (*list)[index].equals(value) {
        copy((*list)[index:], (*list)[index+1:])
        //(*list)[len(*list) - 1] = nil
        *list = (*list)[:len(*list) - 1]
    }
}

//Return value result from bsearch
func (listp * idList)get(value id) id{
    list := *listp
    i := listp.bsearch(value)
    return list[i]
}

//Consistant Hash type, public
type ConsistentHash struct {
    hashes idList //Consistant hash object
    owners map[id]string //used to convert pseudo id to node
    valid map[string]bool //used to keep track of nodes. node name is used as key
    pseudoIDs int //How many ID's a single node has, should be set with SetPseudoIDs function
    replicas int //How many replicas to forward push requests to, should be set with SetReplicas function
    count int //How many nodes are in system. Not sure if needed
    r * rand.Rand
    sync.RWMutex //Ensures object is atomic
}

//Creates a new ConsistentHash with default values of 1 for pseudoIDs and replicas
func New() *ConsistentHash {
    ret := new(ConsistentHash)
    ret.pseudoIDs = defaultPsuedoIDs
    ret.replicas = defaultReplicas
    ret.count = 0
    ret.owners = make(map[id]string)
    ret.valid = make(map[string]bool)
    s := rand.NewSource(time.Now().Unix())
    ret.r = rand.New(s)
    return ret
}

func (c * ConsistentHash) SetPseudoIDs(pseudoIDs int) error {
    if pseudoIDs < 1 {
        return fmt.Errorf("pseudoIDs cannot be negative")
    }
    c.Lock()
    defer c.Unlock()
    c.pseudoIDs = pseudoIDs
    return nil
}

func (c * ConsistentHash) SetReplicas(replicas int) error {
    if c.pseudoIDs < 1 {
        return fmt.Errorf("Replicas cannot be negative")
    }
    c.Lock()
    defer c.Unlock()
    c.replicas = replicas
    return nil
}

func (c * ConsistentHash) GetPseudoIDs() int {
    c.RLock()
    defer c.RUnlock()
    return c.pseudoIDs
}

func (c * ConsistentHash) GetReplicas() int {
    c.RLock()
    defer c.RUnlock()
    return c.replicas
}

func (c * ConsistentHash) GetNumberOfNodes() int {
    c.RLock()
    defer c.RUnlock()
    return c.count
}

func (c * ConsistentHash) AddNode(name string) error {
    c.Lock()
    defer c.Unlock()

    if val, ok := c.valid[name]; ok { //check if name already present
        if val == false {
            c.valid[name] = true
        }
    } else {
        seedID := id(sha256.Sum256([]byte(name)))
        c.valid[name] = true
        c.owners[seedID] = name
        c.hashes.insert(seedID)
        c.count++
        for i := 1; i < c.pseudoIDs; i++ {
            seedID = id(sha256.Sum256(seedID[:]))
            c.owners[seedID] = name
            c.hashes.insert(seedID)
        }
    }
    return nil
}

func (c * ConsistentHash) RemoveNode(name string) {
    c.Lock()
    defer c.Unlock()
    if _, ok := c.valid[name]; ok {
        //Remove from everything
        delete(c.valid, name)
        seedID := id(sha256.Sum256([]byte(name)))
        delete(c.owners, seedID)
        c.hashes.remove(seedID)
        c.count--
        for i := 1; i < c.pseudoIDs; i++ {
            seedID = id(sha256.Sum256(seedID[:]))
            delete(c.owners, seedID)
            c.hashes.remove(seedID)
        }
    }
}

func (c * ConsistentHash) IsValidNode(name string) bool {
    c.RLock()
    defer c.RUnlock()
    if val, ok := c.valid[name]; ok {
        return val
    }
    return false
}

func (c * ConsistentHash) InvalidateNode(name string) {
    c.Lock()
    defer c.Unlock()
    if _, ok := c.valid[name]; ok {
        fmt.Printf("Invalidating: %s\n", name)
        c.valid[name] = false
        c.count--
    }
}

func (c * ConsistentHash) ValidateNode(name string) {
    c.Lock()
    defer c.Unlock()
    if _, ok := c.valid[name]; ok {
        c.valid[name] = true
        c.count++
    }
}

func (c * ConsistentHash) Hash(key string) (string, error) {
    c.RLock()
    defer c.RUnlock()
    idkey, err := fromString(key)
    if err != nil {
        return key, err
    }
    if c.count == 0 {
        return key, fmt.Errorf("No nodes available")
    }
    index := c.hashes.bsearch(idkey)
    pseudoID := c.hashes[index]
    node := c.owners[pseudoID]
    if c.valid[node] == true {
        return node, nil
    }
    firstIndex := index
    for c.valid[node] == false {
        index = index + 1 % len(c.hashes)
        if index == firstIndex {
            return key, fmt.Errorf("No nodes returned from hash")
        }
        pseudoID = c.hashes[index]
        node = c.owners[pseudoID]
    }
    return node, nil
}

func in(list []string, item string) bool {
    for _, v := range list {
        if v == item {
            return true
        }
    }
    return false
}

func (c * ConsistentHash) GetReplicaNodes(key string) ([]string, error) {
    var ret []string
    c.RLock()
    defer c.RUnlock()
    idkey, err := fromString(key)
    if err != nil {
        return ret, err
    }
    if c.count == 0 {
        return ret, fmt.Errorf("No nodes available")
    }
    index := c.hashes.bsearch(idkey)
    pseudoID := c.hashes[index]
    node := c.owners[pseudoID]
    i := 0
    if c.valid[node] == true {
        ret = append(ret, node)
        i++
    }

    firstIndex := index
    for i < c.replicas {
        index = (index + 1) % len(c.hashes)
        if index == firstIndex {
            return ret, nil
        }
        pseudoID = c.hashes[index]
        node = c.owners[pseudoID]
        if c.valid[node] == true && !in(ret, node) {
            ret = append(ret, node)
            i++
        }
    }
    return ret, nil
}

//Randomly selects one node which should have the replica
func (c * ConsistentHash) ReadHash(key string) (string, error) {
    nodes, err := c.GetReplicaNodes(key)
    if err != nil {
        return "", err
    }
    //I don't think rand is thread safe but it shouldn't matter much
    return nodes[c.r.Intn(len(nodes))], nil
}

//Returns list of all valid nodes
func (c * ConsistentHash) GetNodes() []string {
    keys := make([]string, len(c.valid))
    i := 0
    for k, v := range c.valid {
        if v {
            keys[i] = k
            i++
        }
    }
    return keys[:i]
}

//Returns list of all nodes regardless of validity
func (c * ConsistentHash) GetAllNodes() []string {
    keys := make([]string, len(c.valid))
    i := 0
    for k := range c.valid {
        keys[i] = k
        i++
    }
    return keys
}
