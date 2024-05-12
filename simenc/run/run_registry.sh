
#! THIS IS AN EXAMPLE SHOWING HOW TO IMPORT CODE TO CONTAINER AND BUILD AS AN IMAGE
#
docker run -ti -v /home/simenc/go/src/github.com/docker:/docker nnzhaocs/distribution:golang--tag
#
docker commit --change "ENV DEBUG true" 179177d83d0d  nnzhaocs/distribution:src--tag
#
docker push nnzhaocs/distribution:src--tag
#
docker cp 0ca7ddb4282d:/go/src/github.com/docker/simenc/bin/registry ./
#
docker build -t nnzhaocs/distribution:registry .
#
docker stop container_id

#==========================>
ps axf | grep docker | grep -v grep | awk '{print "kill -9 " (}' | sudo sh')}'

docker login

docker push nnzhaocs/distribution:latest
docker tag nnzhaocs/distribution:latest nnzhaocs/socc-sfit-dedup

pssh -h remotehostthors.txt -l root -A -i 'mount -t tmpfs -o size=8G tmpfs /home/simenc/testing/tmpfs'

sudo mount 192.168.0.174:/home/simenc/dockerimages/layers hulk4
sudo mount 192.168.0.171:/home/simenc/dockerimages/layers hulk1
sudo mount 192.168.0.172:/home/simenc/dockerimages/docker-traces/data_centers/ data_centers

find $(pwd) -type f > ../hulk1_layers_less_50m.lst

####:=========== gether docker logs together and extract results =====================
create a new directory in hulk0 with timestamp
mkdir $(date +%Y%m%d_%H%M%S)

pssh -h remotehostshulk.txt -l root -A -i "sshpass -p 'simenc' scp /var/lib/docker/containers/*/*-json.log root@hulk0:/home/simenc/testing/resultslogs/   "

####:=========setup redis cluster ===========
-t 600 'cd /home/simenc/; wget http://download.redis.io/redis-stable.tar.gz; tar xzf redis-stable.tar.gz; cd redis-stable;  make'
'iptables -F'
pssh -h remotehosts.txt -l root -A -i "cd /home/simenc/distribution/run/7001; /home/simenc/redis-stable/src/redis-server ./redis.conf &"
pssh -h remotehosts.txt -l root -A -i "cd /home/simenc/distribution/run/7000; /home/simenc/redis-stable/src/redis-server ./redis.conf &"
redis-cli --cluster create 192.168.0.170:7000 192.168.0.170:7001 \
  192.168.0.171:7000 192.168.0.171:7001 \
  192.168.0.172:7000 192.168.0.172:7001 \
  192.168.0.174:7000 192.168.0.174:7001 \
  192.168.0.176:7000 192.168.0.176:7001 \
  192.168.0.177:7000 192.168.0.177:7001 \
  192.168.0.179:7000 192.168.0.179:7001 \
  192.168.0.180:7000 192.168.0.180:7001 \
  --cluster-replicas 3


redis-cli --cluster create 192.168.0.200:7000 192.168.0.200:7001 \
192.168.0.201:7000 192.168.0.201:7001 \
192.168.0.202:7000 192.168.0.202:7001 \
192.168.0.203:7000 192.168.0.203:7001 \
192.168.0.204:7000 192.168.0.204:7001 \
192.168.0.205:7000 192.168.0.205:7001 \
192.168.0.208:7000 192.168.0.208:7001 \
192.168.0.209:7000 192.168.0.209:7001 \
192.168.0.210:7000 192.168.0.210:7001 \
192.168.0.211:7000 192.168.0.211:7001 \
192.168.0.212:7000 192.168.0.212:7001 \
192.168.0.213:7000 192.168.0.213:7001 \
192.168.0.214:7000 192.168.0.214:7001 \
192.168.0.215:7000 192.168.0.215:7001 \
192.168.0.216:7000 192.168.0.216:7001 \
192.168.0.217:7000 192.168.0.217:7001 \
192.168.0.218:7000 192.168.0.218:7001 \
192.168.0.219:7000 192.168.0.219:7001 \
192.168.0.221:7000 192.168.0.221:7001 \
192.168.0.222:7000 192.168.0.222:7001 \
192.168.0.223:7000 192.168.0.223:7001 \
--cluster-replicas 3

docker rmi $(docker images -a -q)
####:==========cleanup for hulks =============
pssh -h remotehostshulk.txt -l root -A -i 'docker stop $(docker ps -a -q)'
pssh -h remotehostshulk.txt -l root -A -i 'docker rm $(docker ps -a -q)'
pssh -h remotehostshulk.txt -l root -A -i 'docker ps -a'
pssh -h remotehostshulk.txt -l root -A -i 'ls /var/lib/docker/containers/'
pssh -h remotehostshulk.txt -l root -A -i 'rm -rf /home/simenc/testing/tmpfs/*'
pssh -h remotehostshulk.txt -l root -A -i 'rm -rf /home/simenc/testing/layers/*'

./flushall-cluster.sh 192.168.0.170

####: =========run dedupregistrycache ==============
pssh -h remotehostthors.txt -l root -A -i 'docker run --rm -d -p 5000:5000 --mount type=bind,source=/home/simenc/testing/tmpfs,target=/var/lib/registry/docker/registry/v2/pull_tars/ -v=/home/simenc/testing/layers:/var/lib/registry -e  "REGISTRY_STORAGE_CACHE_HOSTIP=$(ip -4 addr |grep 192.168.0.2 |grep -Po "inet \K[\d.]+")" --name registrydedupcache nnzhaocs/distribution:distributionlrucache'

####:==========run siftregistry ==================
#sudo docker run -p 5000:5000 -d --rm --mount type=bind,source=/home/simenc/testing/tmpfs,target=/var/lib/registry/docker/registry/v2/pull_tars/ -v /home/simenc/testing/layers:/var/lib/registry -e "REGISTRY_STORAGE_CACHE_HOSTIP=$(ip -4 addr |grep 192.168 |grep -Po 'inet \K[\d.]+')" --name dedup-test -t nnzhaocs/distribution:sift
pssh -h remotehostshulk.txt -l root -A -i 'docker run --rm -d -p 5000:5000 --mount type=bind,source=/home/simenc/testing/tmpfs,target=/var/lib/registry/docker/registry/v2/pull_tars/ -v=/home/simenc/testing/layers:/var/lib/registry -e "REGISTRY_STORAGE_CACHE_HOSTIP=$(ip -4 addr |grep 192.168 |grep -Po "inet \K[\d.]+")" --name siftdedup-3  nnzhaocs/distribution:siftdedup'


####:============run traditionaldedupregistrycluster======================######
#sudo docker service create --name traditionaldedupregistry --replicas 10 --mount type=bind,source=/home/simenc/testing/tmpfs,target=/var/lib/registry/docker/registry/v2/pull_tars/ -v /home/simenc/testing/layers:/var/lib/registry -e "REGISTRY_STORAGE_CACHE_HOSTIP=$(ip -4 addr |grep 192.168 |grep -Po 'inet \K[\d.]+')"

#####: For thors
#pssh -h remotehostshulk.txt -l root -A -i 'docker run --rm -d -p 5000:5000 --mount type=bind,source=/home/simenc/testing/tmpfs,target=/var/lib/registry/docker/registry/v2/pull_tars/ -v=/home/simenc/testing/layers:/var/lib/registry -e "REGISTRY_STORAGE_CACHE_HOSTIP=$(ip -4 addr |grep 192.168.0.2 |grep -Po "inet \K[\d.]+")" --name traditionaldedup-3  nnzhaocs/distribution:traditionaldedup'
#pssh -h remotehostthors.txt -l root -A -i 'docker run --rm -d -p 5000:5000 --mount type=bind,source=/home/simenc/testing/tmpfs,target=/var/lib/registry/docker/registry/v2/pull_tars/ -v=/home/simenc/testing/layers:/var/lib/registry -e "REGISTRY_STORAGE_CACHE_HOSTIP=$(ip -4 addr |grep 192.168.0.2 |grep -Po "inet \K[\d.]+")" --name traditionaldedup-3 --net=host nnzhaocs/distribution:traditionaldedup'

1. cleanup hulks same as before, and cleanup amaranths as:
pssh -h remotehostsamaranth.txt -l root -A -i 'docker stop $(docker ps -a -q)'
pssh -h remotehostsamaranth.txt -l root -A -i 'rm -rf /home/simenc/testing/layers/*'

pssh -h remotehostshulk.txt -l root -A -i 'docker stop $(docker ps -a -q)'
pssh -h remotehostshulk.txt -l root -A -i 'rm -rf /home/simenc/testing/tmpfs/*'
pssh -h remotehostshulk.txt -l root -A -i 'rm -rf /home/simenc/testing/layers/*'

./flushall-cluster.sh 192.168.0.170

2. setup amaranth registries first:
pssh -h remotehostsamaranth.txt -l root -A -i 'docker run --rm -d -p 5000:5000 -v=/home/simenc/testing/layers:/var/lib/registry --name random-registry-cluster registry'

3. setup hulk registries:
pssh -h remotehostshulk.txt -l root -A -i 'docker run --rm -d -p 5000:5000 --mount type=bind,source=/home/simenc/testing/tmpfs,target=/var/lib/registry/docker/registry/v2/pull_tars/ -v=/home/simenc/testing/layers:/var/lib/registry -e "REGISTRY_STORAGE_CACHE_HOSTIP=$(ip -4 addr |grep 192.168 |grep -Po "inet \K[\d.]+")" --name traditionaldedupregistry-3  nnzhaocs/distribution:traditionaldedup'

4. run docker-performance:
config_1.yaml configuration:
traditionaldedup: True; others are set to false; warmup threads: 5;
Others same as before.


#5. save parameters:
#Two kinds of values: ones start with "Blob:File:Recipe::sha256" and ones start with "Blob:File:Recipe::RestoreTime::sha256"

#For the ones with "Blob:File:Recipe::RestoreTime::sha256*"
#we need to save:
BlobDigest:
UncompressSize:
#CompressSize:

#For the ones with "Blob:File:Recipe::sha256*"
#we need to save
key
SliceSize
DurationCP
DurationCMP
DurationML
DurationNTT
DurationRS

So inaddition to value fields, we need to save the key as well for "Blob:File:Recipe::sha256*"

####:============run originalregistrycluster======================######

pssh -h remotehosts.txt -l root -A -i 'docker run --rm -d -p 5000:5000 --mount type=bind,source=/home/simenc/testing/tmpfs,target=/var/lib/registry/docker/registry/v2/pull_tars/ -v=/home/simenc/testing/layers:/var/lib/registry -e "REGISTRY_STORAGE_CACHE_HOSTIP=$(ip -4 addr |grep 192.168 |grep -Po "inet \K[\d.]+")" --name originalregistry-3  nnzhaocs/distribution:original'


####: ============run randomregistrycluster on amaranths============#####
sudo docker service create --name randomregistry --replicas 5 --mount type=bind,source=/home/simenc/testing/layers,destination=/var/lib/registry -p 5000:5000 registry

#=========================> HOW TO RUN REDIS WITH REGISTRY <=====================
docker run -p 6379:6379 --name redis-rejson redislabs/rejson:latest
redis-cli FLUSHALL

sudo netstat -plnto

https://linuxize.com/post/how-to-stop-and-disable-firewalld-on-centos-7/
iptables -t filter -N DOCKER


#### =============================> SETUP centralized log <============================


