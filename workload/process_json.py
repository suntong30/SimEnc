import json
import os
import random

def j_get_file_sizes(directory):
    file_sizes = {}
    for root, dirs, files in os.walk(directory):
        for file in files:
            file_path = os.path.join(root, file)
            file_size = os.path.getsize(file_path)

            if file_size not in file_sizes:
                file_sizes[file_size] = [file_path]
            else:
                file_sizes[file_size].append(file_path)
    return file_sizes

def get_continoous_data(f_path):
    path_list = []
    for root, dirs, files in os.walk(f_path):
        for file in files:
            file_path = os.path.join(root, file)
            path_list.append(file_path.replace(f_path,""))
    return path_list

use_size = {}
def process_item(item, size_dic, our_path_prefix ,max_num ,minsize):
    method = item["http.request.method"]
    uri = item["http.request.uri"]
    if method == "GET" and ( "blobs" in uri):
        resopnse_size = item["http.response.written"]
        if  resopnse_size > minsize and  resopnse_size in size_dic:
            if resopnse_size not in use_size:
                use_size[resopnse_size] = 0
            elif  use_size[resopnse_size] >= max_num:
                return None
            else :
                use_size[resopnse_size] += 1
            our_path = random.choice(size_dic[resopnse_size])

            # 在our_path中去掉原始的前缀
            our_path = our_path.replace(our_path_prefix, "")
            sha_256 = our_path.split("/")[-1].replace(".tar.gz", "")
            repo_name = our_path.replace(f"/{sha_256}.tar.gz", "")
            item["j_http.request.uri"] = f"v2/{repo_name}/blobs/{sha_256.replace('_', ':')}"
            item["j_repo"] = repo_name
            item["sha_256"] = sha_256
            return item
    return None


def get_json_data(json_name , size_dic,our_path_prefix):
    # 读取原始JSON文件
    with open(json_name, 'r') as file:
        data = json.load(file)

    # 处理每个项
    filtered_data = [process_item(item, size_dic,our_path_prefix) for item in data]

    # 过滤掉处理后为 None 的项
    filtered_data = [item for item in filtered_data if item is not None]

def get_want_json_data(json_path , size_dic , want_num,out_json_name ,our_path_prefix, hot_t , max_num , minsize) :
    # 写入筛选后的数据到新的JSON文件
    final_data = []
    num = 0
    #遍历文件夹下所有文件
    json_list = os.listdir(json_path)
    for json_name in json_list:
        num += 1
        # 读取原始JSON文件
        with open(os.path.join(json_path, json_name), 'r') as file:
            data = json.load(file)
        # 处理每个项
        filtered_data = [process_item(item, size_dic, our_path_prefix ,max_num , minsize ) for item in data]
        # 过滤掉处理后为 None 的项
        filtered_data = [item for item in filtered_data if item is not None]
        final_data += filtered_data
        if len( final_data ) >= want_num:
            break
    final_data = final_data[:want_num]

    # 与duphunter flexible模式进行对比实验
    # 按照duphunter的策略，假设我们全局已知每个层的热度，对于高热度层不进行dedup
    # 统计重复的layer层，添加处理
    #
    # dic_info = {}
    # hot_layers = []
    # for item in final_data:
    #     cururi = item['j_http.request.uri']
    #     if cururi in dic_info:
    #         dic_info[cururi] += 1
    #     else:
    #         dic_info[cururi] = 0
    #     if dic_info[cururi] >= hot_t:
    #         hot_layers.append(cururi)
    
    # for item in final_data:
    #     if item['j_http.request.uri'] in hot_layers:
    #         repo_name = item['j_repo']
    #         new_repo_name = repo_name + "_nodedup"
    #         item['j_repo'] = item['j_repo'].replace(repo_name, new_repo_name)
    #         item['j_http.request.uri'] = item['j_http.request.uri'].replace(repo_name, new_repo_name)
        

    with open( out_json_name, 'w') as file:
        json.dump(final_data, file, indent=2)
    print("Filtering completed with data = {} use file_num : {}.".format(len(final_data),num))

def main():
    our_data_path = "./docker_data"
    raw_json_folder = "./DockerRegistryTraces/data_centers/syd01"
    
    want_num = 2000
    # 仅在与duphunter对比时使用，
    # hot_t = -1 时代表所有layer都是热度文件(不进行去重) ，值越大 -> 越多文件进行去重
    hot_t = want_num  
    # 允许同一个size 在trace数据集中最多出现的次数
    max_num = 10
    # 进行测试的最小 layer size 
    min_size = 800 
    out_json_name = f"{raw_json_folder.split('/')[-1]}_{our_data_path.split('/')[-1]}_{want_num}_{max_num}_ht{hot_t}_ms{min_size}.json"
    dic_info = j_get_file_sizes(our_data_path)
    get_want_json_data(raw_json_folder , dic_info ,want_num , out_json_name , our_data_path + "/" , hot_t ,max_num ,min_size)


if __name__ == "__main__":
    main()