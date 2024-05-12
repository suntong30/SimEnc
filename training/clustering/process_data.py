import os
import shutil
import sys

if __name__ == '__main__':
# 输入文件和索引列表
    big_file_path = sys.argv[1] # 替换为实际的大文件路径
    index_file_path = sys.argv[2]  # 替换为包含索引的文本文件路径

    # 输出文件夹
    output_folder = 'input_data_partial'
    if not os.path.exists(output_folder):
        os.makedirs(output_folder)

    # 读取索引列表
    class_indices = []
    with open(index_file_path, 'r') as index_file:
        for line in index_file:
            class_indices.append([int(idx) for idx in line.strip().split()[1:]])

    # 分块大小
    block_size = 512 * 1024  # 4K

    # 切割大文件并组织数据块
    for class_id, indices in enumerate(class_indices):
        class_folder = os.path.join(output_folder, f'{class_id}')
        os.makedirs(class_folder, exist_ok=True)

        for idx in indices:
            offset = idx * block_size
            data_block = bytearray()

            with open(big_file_path, 'rb') as big_file:
                big_file.seek(offset)
                data_block = big_file.read(block_size)

            data_block_path = os.path.join(class_folder, f'{class_id}_{idx}.bin')
            with open(data_block_path, 'wb') as data_block_file:
                data_block_file.write(data_block)

    print("Data blocks have been organized and saved.")
