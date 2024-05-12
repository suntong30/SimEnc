package ConsistentHash_Sha256
/*
package main

import (
    ch "./consistent_hash"
    other "stathat.com/c/consistent"
    "math/rand"
    "fmt"
)

const million = 1000000

func main() {
    fmt.Println("hello")
    c1 := ch.New()
    c1.SetPseudoIDs(50)
    c2 := other.New()
    nodes := []string{"node1", "node2", "node3", "node4", "node5"}
    sid := make([]byte, 32)
    myc := make(map[string]int)
    theirc := make(map[string]int)
    for _, i := range nodes {
        fmt.Printf("node: %s\n", i)
        myc[i] = 0
        theirc[i] = 0
        c2.Add(i)
        c1.AddNode(i)
    }
   for i := 0; i < million; i++ {
        rand.Read(sid)
        key := fmt.Sprintf("%x", sid)
        h1, err := c1.Hash(key)
        if err != nil {
            fmt.Printf("%v\n", err)
        }
        h2, _ := c2.Get(key)
        myc[h1]++
        theirc[h2]++
    }
    mystats := make([]float64, 5)
    theirstats := make([]float64, 5)
    for i, node := range nodes {
        mystats[i] = float64(myc[node]) / float64(million)
        theirstats[i] = float64(theirc[node]) / float64(million)
    }
    fmt.Printf("My stats:\n")
    for _, v := range mystats {
        fmt.Printf("%v\n", v)
    }
    fmt.Printf("Their stats:\n")
    for _, v := range theirstats {
        fmt.Printf("%v\n", v)
    }

}*/
