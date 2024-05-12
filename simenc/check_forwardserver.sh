grep "will be forwarded to server" log > ser
awk -F'will be forwarded to server \(' '{print $2}' ser > ser1
awk -F'\)' '{print $1}' ser1>ser

grep "slice cache put" log > size
#awk -F'slice cache put:' size>size1
awk -F'slice cache put:' '{print $2}' size>size1
awk -F'"' '{print $1}' size1 > size
