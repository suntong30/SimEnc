
#!/bin/sh

NODES=`redis-cli -h $1 -p 7000 cluster nodes | cut -f2 -d' '`

IFS="
"

for node in $NODES; do
        echo Flushing node $node...
            redis-cli -h ${node%:*} -p ${node##*:} flushall
        done
echo Flushing node 192.168.0.220
redis-cli -h 192.168.0.220 flushall
