# SimEnc

## Introduction

SimEnc is a high-performance similarity-preserving encryption approach for deduplication of encrypted Docker images. This repo contains the implementation of SimEnc prototype, baseline approaches, and a trace analysis tool used in our USENIX ATC 2024 paper.

- `./simenc`: includes the codes of simenc prototype.
- `./workload`: includes the relevant crawler and data mapping code.
- `./partial`: includes  partial decode/encode tools , which are used  for processing files in gzip format.
- `./warmup_and_run`: includes configuration files and experimental code related to warmup and run phases.

## Build

We completed our code based on the duphunter project, and we adopted the same construction process, which you can refer to [DupHunter/README.md](https://github.com/nnzhaocs/DupHunter/blob/master/README.md)
## Dependency

### 1.Workload

We build the workload in this experiment based on [chalianwar/docker-performance](https://github.com/chalianwar/docker-performance) .The Docker registry trace player is used to replay anonimized production level traces for a registry, available in the traces directory. The traces are from the IBM docker registry, separated by availability zones. 

The traces are available at http://research.cs.vt.edu/dssl/drtp-head.html. The size of the traces is 22 GB when unzipped.

Since the image names in this data set are anonymous, in order to obtain a more accurate data deduplication rate in real workloads, we downloaded a large amount of image data through a python crawler. Then the same file size is mapped to our data through "http.write.size" in the original record, so that we can use the real user behavior in the trace record  to calculate the deduplication rate of our system on real workload.

The relevant crawler and data mapping code can be found in folder **`./workload`**

### 2.Partial_decode_encode tool

The relevant code for partial decode/encode can be found in folder **`./partial`**. You can follow the following steps to compile and generate the **encode** and **decode**  executable files required by the system.

```shell
cd ./partial
make
```

Then you can get **libz_j.a** static link library

```shell
g++ decode_docker.cpp -o decode -L. libz_j.a
g++ encode_docker.cpp -o encode -L. libz_j.a
```

After completing the above steps, you can find the partial decode/encode tool required by the system in the current folder.

### 3.Redis

We use redis cluster for caching in our system. You can complete a simple cluster configuration according to the following steps.

Download the redis source code from [Download | Redis](https://redis.io/download/).

```shell
tar -xzvf redis-x.x.x.tar.gz
cd redis-x.x.x
make
```

Then you can find the executable file **redis-serve** and the cluster management tool **redis-trib.rb** in the *redis-x.x.x/src* directory.

If you have started 6 redis-server instances on local ports 6379~6384, you can start the cluster through the following command.

```shell
./redis-trib.rb create --replicas 1 127.0.0.1:6379 127.0.0.1:6380 127.0.0.1:6381 127.0.0.1:6382 127.0.0.1:6383 127.0.0.1:6384
```

## Run

### 1. Prepare

- Prepare the docker data, and then use `./workload/process_json.py` files to complete the data mapping
- Compile the project, and if successful you will find the ***registry*** executable file in the**` ./bin`** folder
- Put your compiled ***encode*** and ***decode*** executable file into the**`./bin`** folder.
- (Optional) Start the Redis cluster
- set the **` ./bin/config.yaml`**

### 2. Start registry service

```shell
cd ./Secd2/bin
./registry serve config.yaml
```

### 3. Warmup(pull layer) and Run(get layer)

The relevant code for warmup and run can be found in folder *./warmup_and_run*

```shell
# warmup phase
python3 warmup_run.py -c warmup -i config.yaml
```

```shell
# run phase
python3 client.py -i 127.0.0.1 -p 8081
python3 warmup_run.py -c run -i config.yaml
```

## Result

- **Latency**: Once the configuration file is set up, the latency statistics will be output in the shell after a trace data replay is completed during the warmup and run phases.(such as table 3, figure 11)

```shell
# warmup phase
# python3 warmup_run.py -c warmup -i config.yaml
# you will get output like:
warmup_time = 300
warup_size = 10,000,000,000
nowsize = 5,000,000,000 , dedup ration = 2
```

```shell
# run phase
# python3 warmup_run.py -c run -i config.yaml
# you will get output like:
Successful Requests = 10000
Failed Request = 0
Duration = 50
Data Transfer = 10,000,000,000 bytes
Average Latency : 0.5
Throughput : 200 requests/second
```

**Note**: The output here is the average statistics, in fact every request will record latency in the log, you can get the percentile statistics based on the log

- **Storage**: We used the *`du`* command base on Linux system to calculate the actual storage size(such as table 4, figure 10)

