import sys
import socket
import os
from argparse import ArgumentParser
import subprocess
import requests
import time
import datetime
import random
import threading
import multiprocessing
import json 
import yaml

from dxf import *
from multiprocessing import Process, Queue
import importlib
import hashring
#import hash_ring


our_data_path_prefix = "/home/jbw/EMNETS/docker_dup/data"

def get_total_size(folder_path):
    output = subprocess.getoutput(f"du -b -s {folder_path}")
    total_size = int(output.split()[0])
    return total_size



## get requests
def send_request_get(client, payload):
    ## Read from the queue 
    s = requests.session()
    headers = {'Content-type': 'application/json', 'Accept': 'text/plain'}
    print("http://" + str(client) + "/up")
    s.post("http://" + str(client) + "/up", data=json.dumps(payload), headers=headers, timeout=100)


def j_send_warmup_thread(requests, q, registry, path_q):
    trace = {}
    path_trace = {}
    no = 0
    num = 0
    for request in requests:
        no += 1
        print(f"----------- {no} ------------")
        repo_name = request["repo_name"]
        uri = request["uri"]
        dxf = DXF(registry, repo_name, insecure=True)
        if not ( uri in trace):
            num += 1
            our_reponame = repo_name.replace("_nodedup","")
            our_data_path = f"{our_data_path_prefix}/{our_reponame}/{uri.split('/')[-1].replace(':','_')}.tar.gz" 
            try:
                dgst = dxf.push_blob(our_data_path,check_exists= False)
            except Exception as e:
                print(f"Exception : {e}")
                dgst = 'bad'
            trace[uri] = dgst
            path_trace[uri] = our_data_path

    q.put(trace)
    path_q.put(path_trace)

def warmup(data, out_trace, registry, threads):
    star_time = time.time()   
    trace = {}
    path_trace = {}
    processes = []
    q = Queue()
    path_q = Queue()
    process_data = []
    used_uri = []
    used_sha256 = []
    for i in range(threads):
        process_data.append([])
    actnum = 0
    store_size = 0

    data_id = 0
    for request in data:
            data_id += 1 
            cururi = request['uri']

            if cururi not in used_uri:
                process_data[data_id % threads].append(request)
                store_size += request['size']
                actnum += 1
                used_uri.append(cururi)
    print(f"actnum = {actnum}")    
    print(f"warmup size = {store_size} ")
    for i in range(threads):
        p = Process(target = j_send_warmup_thread, args=(process_data[i], q, registry ,path_q))
        processes.append(p)

    for p in processes:
        p.start()

    for i in range(threads):
        d = q.get()
        for thing in d:
            if thing in trace:
                if trace[thing] == 'bad' and d[thing] != 'bad':
                    trace[thing] = d[thing]
            else:
                trace[thing] = d[thing]
        
        d = path_q.get()
        for thing in d:
            path_trace[thing] = d[thing]
            
    for p in processes:
        p.join()
    with open(out_trace, 'w') as f:
        json.dump(trace, f)
    with open("j_trace_path.json", 'w') as f:
        json.dump(path_trace, f)
    print(f"actnum = {actnum}")
    print(f"warmup_time = {time.time()  - star_time}") 
    print(f"warmup size = {store_size} ")

    current_folder = '/home/jbw/docker_v2/docker/registry/v2/blobs'  # 替换成想要镜像实际存储的文件夹路径
    # 等待服务器做完dedup
    time.sleep(5)
    now_size = get_total_size(current_folder)
    print("nowsize = {} , dedup ration = {} ".format(now_size , store_size * 1.0 / now_size ) )
     
    
    
 
def stats(responses):
    responses.sort(key = lambda x: x['time'])

    endtime = 0
    data = 0
    latency = 0
    total = len(responses)
    onTimes = 0
    failed = 0
    startTime = responses[0]['time']
    for r in responses:
        if r['onTime'] == 'failed':
            total -= 1
            failed += 1
            continue
        if r['time'] + r['duration'] > endtime:
            endtime = r['time'] + r['duration']
        latency += r['duration']
        data += r['size']
        if r['onTime'] == 'yes':
            onTimes += 1
    duration = endtime - startTime
    print ('Statistics')
    print ('Successful Requests: ' + str(total))
    print ('Failed Requests: ' + str(failed))
    print ('Duration: ' + str(duration))
    print ('Data Transfered: ' + str(data) + ' bytes')
    print ('Average Latency: ' + str(latency / total))
    print ('% requests on time: ' + str(1.*onTimes / total))
    print ('Throughput: ' + str(1.*total / duration) + ' requests/second')

           
def serve(port, ids, q, out_file):
    server_address = ("0.0.0.0", port)
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.bind(server_address)
        sock.listen(len(ids))
    except:
        print ("Port already in use: " + str(port))
        q.put('fail')
        quit()
    q.put('success')
 
    i = 0
    response = []
    print ("server waiting")
    while i < len(ids):
        connection, client_address = sock.accept()
        resp = ''
        while True:
            r = connection.recv(1024)
            if not r:
                break
            str_r = r.decode('utf-8')
            resp += str_r
        connection.close()
        try:
            print(resp)
            info = json.loads(resp)
            if info[0]['id'] in ids:
                info = info[1:]
                response.extend(info)
                i += 1
        except:
            print ('exception occured in server')
            pass

    with open(out_file, 'w') as f:
        json.dump(response, f)
    print( 'results written to ' + out_file)
    stats(response)

  
## Get blobs
def get_blobs(data, clients_list, port, out_file):
    print("get_blioc")
    # print(data)
    processess = []

    ids = []
    for d in data:
        ids.append(d[0]['id'])

    serveq = Queue()
    server = Process(target=serve, args=(port, ids, serveq, out_file))
    server.start()
    status = serveq.get()
    if status == 'fail':
        quit()
    ## Lets start processes
    i = 0
    for client in clients_list:
        p1 = Process(target = send_request_get, args=(client, data[i], ))
        processess.append(p1)
        i += 1
        print( "starting client ...")
    for p in processess:
        p.start()
    for p in processess:
        p.join()

    server.join()

def get_requests(files, t, limit):
    ret = []
    for filename in files:
        with open(filename, 'r') as f:
            requests = json.load(f)
    
        for request in requests:
            uri = request['j_http.request.uri']
            timestamp = datetime.datetime.strptime(request['timestamp'], '%Y-%m-%dT%H:%M:%S.%fZ')
            duration = request['http.request.duration']
            client = request['http.request.remoteaddr']
            size = request['http.response.written']
            repo_name = request['j_repo']
            r = {
                        'delay': timestamp, 
                        'uri': uri, 
                        'size': size, 
                        'duration': duration,
                        'client': client,
                        'repo_name':repo_name
                    }
            ret.append(r)
    ret.sort(key= lambda x: x['delay'])
    begin = ret[0]['delay']

    for r in ret:
        r['delay'] = (r['delay'] - begin).total_seconds()
   
    if t == 'seconds':
        begin = ret[0]['delay']
        i = 0
        for r in ret:
            if r['delay'] > limit:
                break
            i += 1
        print( i )
        return ret[:i]
    elif t == 'requests':
        return ret[:limit]
    else:
        return ret

def organize(requests, out_trace, numclients, client_threads, port, wait, registries, round_robin, push_rand):
    organized = []

    if round_robin is False:
        ring = hashring.HashRing(range(numclients))
    with open(out_trace, 'r') as f:
        blob = json.load(f)

    for i in range(numclients):
        organized.append([{'port': port, 'id': random.getrandbits(32), 'threads': client_threads, 'wait': wait, 'registry': registries, 'random': push_rand}])
        print (organized[-1][0]['id'])
    i = 0

    for r in requests:
        request = {
            'delay': r['delay'],
            'duration': r['duration'],
            'repo_name': r['repo_name']
        }
        if r['uri'] in blob:
            b = blob[r['uri']]
            if b != 'bad':
                request['blob'] = b
                request['method'] = 'GET'
                if round_robin is True:
                    organized[i % numclients].append(request)
                    i += 1
                else:
                    organized[ring.get_node(r['client'])].append(request)
        else:
            request['size'] = r['size']
            request['method'] = 'PUT'
            if round_robin is True:
                organized[i % numclients].append(request)
                i += 1
            else:
                organized[ring.get_node(r['client'])].append(request)

    return organized


def main():

    parser = ArgumentParser(description='Trace Player, allows for anonymized traces to be replayed to a registry, or for caching and prefecting simulations.')
    parser.add_argument('-i', '--input', dest='input', type=str, required=True, help = 'Input YAML configuration file, should contain all the inputs requried for processing')
    parser.add_argument('-c', '--command', dest='command', type=str, required=True, help= 'Trace player command. Possible commands: warmup, run, simulate, warmup is used to populate the registry with the layers of the trace, run replays the trace, and simulate is used to test different caching and prefecting policies.')

    args = parser.parse_args()
    
    # config = file(args.input, 'r')
    config = open(args.input,'r')

    try:
        inputs = yaml.load(config, Loader=yaml.FullLoader)
    except Exception as inst:
        print ('error reading config file')
        print (inst)
        exit(-1)
    print(inputs)
    verbose = False

    if 'verbose' in inputs:
        if inputs['verbose'] is True:
            verbose = True
            print ('Verbose Mode')

    if 'trace' not in inputs:
        print ('trace field required in config file')
        exit(1)

    trace_files = []

    if 'location' in inputs['trace']:
        location = inputs['trace']['location']
        if '/' != location[-1]:
            location += '/'
        for fname in inputs['trace']['traces']:
            trace_files.append(location + fname)
    else:
        trace_files.extend(inputs['trace']['traces'])

    if verbose:
        print( 'Input traces')
        for f in trace_files:
            print (f)

    limit_type = None
    limit = 0

    if 'limit' in inputs['trace']:
        limit_type = inputs['trace']['limit']['type']
        if limit_type in ['seconds', 'requests']:
            limit = inputs['trace']['limit']['amount']
        else:
            print ('Invalid trace limit_type: limit_type must be either seconds or requests')
            exit(1)
    elif verbose:
        print ('limit_type not specified, entirety of trace files will be used will be used.')

    if 'output' in inputs['trace']:
        out_file = inputs['trace']['output']
    else:
        out_file = 'output.json'
        if verbose:
            print ('Output trace not specified, ./output.json will be used')

    generate_random = False
    if args.command != 'simulate':
        if "warmup" not in inputs or 'output' not in inputs['warmup']:
            print ('warmup not specified in config, warmup output required. Exiting')
            exit(1)
        else:
            interm = inputs['warmup']['output']
            if 'random' in inputs['warmup']:
                if inputs['warmup']['random'] is True:
                    generate_random = True

    registries = []
    if 'registry' in inputs:
        registries.extend(inputs['registry'])

    json_data = get_requests(trace_files, limit_type, limit)

    if args.command == 'warmup':
        if verbose: 
            print( 'warmup mode')
        if 'threads' in inputs['warmup']:
            threads = inputs['warmup']['threads']
        else:
            threads = 1
        if verbose:
            print( 'warmup threads: ' + str(threads))
        warmup(json_data, interm, registries[0], threads)

    elif args.command == 'run':
        if verbose:
            print( 'run mode')

        if 'client_info' not in inputs or inputs['client_info'] is None:
            print ('client_info required for run mode in config file')
            print ('exiting')
            exit(1)

        if 'port' not in inputs['client_info']:
            if verbose:
                print( 'master server port not specified, assuming 8080')
                port = 8080
        else:
            port = inputs['client_info']['port']
            if verbose:
                print ('master port: ' + str(port))

        if 'threads' not in inputs['client_info']:
            if verbose:
                print ('client threads not specified, 1 thread will be used')
            client_threads = 1
        else:
            client_threads = inputs['client_info']['threads']
            if verbose:
                print (str(client_threads) + ' client threads')

        if 'client_list' not in inputs['client_info']:
            print ('client_list entries are required in config file')
            exit(1)
        else:
            client_list = inputs['client_info']['client_list']

        if 'wait' not in inputs['client_info']:
            if verbose:
                print( 'Wait not specified, clients will not wait')
            wait = False
        elif inputs['client_info']['wait'] is True:
            wait = True
        else:
            wait = False

        round_robin = True
        if 'route' in inputs['client_info']:
            if inputs['client_info']['route'] is True:
                round_robin = False
        data = organize(json_data, interm, len(client_list), client_threads, port, wait, registries, round_robin, generate_random)
        ## Perform GET
        get_blobs(data, client_list, port, out_file)


    elif args.command == 'simulate':
        if verbose:
            print ('simulate mode')
        if 'simulate' not in inputs:
            print ('simulate file required in config')
            exit(1)
        pi = inputs['simulate']['name']
        if '.py' in pi:
            pi = pi[:-3]
        try:
            plugin = importlib.import_module(pi)
        except Exception as inst:
            print ('Plugin did not work!')
            print( inst)
            exit(1)
        try:
            if 'args' in inputs['simulate']:
                plugin.init(json_data, inputs['simulate']['args'])
            else:
                plugin.init(json_data)
        except Exception as inst:
            print( 'Error running plugin init!')
            print (inst)


if __name__ == "__main__":
    main()
