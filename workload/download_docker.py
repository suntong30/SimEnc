import requests
import os
import sys
import requests


max_down_size = 10 * 1024 * 1024 *1024
cur_down_size = 0

def get_latest_tag(image_name):
    response = requests.get(f"https://registry.hub.docker.com/v2/repositories/{image_name}/tags/")
    tags = response.json()
    print(tags)
    result  = []
    if len(tags) != 4 or tags['count'] == 0:
        print("not found tag")
        return result
    getnum = 0
    while tags['next'] != None and getnum < 50:
        next_url = tags['next']
        tagsnum = len(tags['results'])
        for i in range(tagsnum):
            latest_tag = tags['results'][i]
            result.append(latest_tag['name'])
            print(latest_tag['name'])
            getnum += 1
        newresponse = requests.get(next_url)
        tags = newresponse.json()
        if len(tags) != 4 or tags['count'] == 0:
            return result
    return result




def download_image_layers(image_name, have_write_file):
    
    global cur_down_size
    print("----------{}----------".format(image_name))

    response = requests.get(
        f"https://auth.docker.io/token?service=registry.docker.io&scope=repository:{image_name}:pull")
    token = response.json()["token"]

    #print(token)
    headers = {
        "Authorization": f"Bearer {token}",
        "Accept": "application/vnd.docker.distribution.manifest.v2+json"
    }
    tag = get_latest_tag(image_name)
    if tag == []:
        with open(have_write_file,'a') as file:
            file.write(image_name + "\n")
        return
    for t in tag:
        cmd = "https://registry-1.docker.io/v2/{}/manifests/{}".format(image_name,t)
        print(cmd)
        response = requests.get(cmd, headers=headers)
        manifest = response.json()

        
        if "layers" not in manifest:
            with open(have_write_file,'a') as file:
                file.write(image_name + "\n")
            return
        for layer in manifest["layers"]:
            digest = layer["digest"]
            response = requests.get(f"https://registry-1.docker.io/v2/{image_name}/blobs/{digest}", headers=headers,
                                    stream=True)

            # 保存到文件
            curfile_path = "./data/{}/{}".format(image_name,t)
            if not os.path.exists(curfile_path):
                os.system("mkdir -p {}".format(curfile_path))
            filename = os.path.join(curfile_path, digest.replace(":", "_") + ".tar.gz")
            with open(filename, 'wb') as f:
                for chunk in response.iter_content(chunk_size=8192):
                    f.write(chunk)
                    cur_down_size += len(chunk)
            print(f"Downloaded layer {digest} to {filename}")
        with open(have_write_file,'a') as file:
            file.write(image_name + "\n")
        print("{} finish".format(image_name + "_" + t))


if __name__ == "__main__":
    file_path = sys.argv[1]
    have_write_file = sys.argv[2]
    if not os.path.exists(have_write_file):
        os.system("touch {}".format(have_write_file))

    with open(file_path,'r') as file:
        lines = file.readlines()
    print("all images have {}".format(len(lines)))
    with open(have_write_file,'r') as file:
        have_lines = file.readlines()
    print("have download {} images".format(len(have_lines)))


    have_data_list = [line.strip() for line in have_lines]
    data_list = [line.strip()  for line in lines if line.strip() not in have_data_list]
    print("will download {} images".format(len(data_list)))
    for img in data_list:
        print("have download size : {}".format(cur_down_size))
        download_image_layers(img, have_write_file)
        if cur_down_size >= max_down_size :
            print(f"downsize > {max_down_size} , break")
            break
