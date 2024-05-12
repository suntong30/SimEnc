#echo "usage: host port"
nodes_addrs=$(redis-cli -h $1 -p $2 cluster nodes|grep -v handshake| awk '{print $2}')
echo $nodes_addrs
for addr in ${nodes_addrs[@]}; do
    host=${addr%:*}
    port=${addr#*:}
    del_nodeids=$(redis-cli -h $host -p $port cluster nodes|grep -E 'handshake|fail'| awk '{print $1}')
    for nodeid in ${del_nodeids[@]}; do
        echo $host $port $nodeid
        redis-cli -h $host -p $port cluster forget $nodeid
    done
done
