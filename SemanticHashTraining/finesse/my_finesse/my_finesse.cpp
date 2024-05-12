#include <iostream>
#include <fstream>
#include <vector>
#include "finesse.h"  // 包含 Finesse 类的定义
#define BLOCK_SIZE 4096  // 定义块大小为4KB

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " [input_file]" << std::endl;
        return 1;
    }

    // 窗口大小、超特征数、特征数可以根据实际情况调整
    int W = 48;             
    int SF_NUM = 3;        
    int FEATURE_NUM = 12;  
    Finesse finesse(BLOCK_SIZE, W, SF_NUM, FEATURE_NUM);

    // 读取4KB的文件块
    std::vector<unsigned char> block(BLOCK_SIZE);
    std::ifstream file(argv[1], std::ios::binary);
    if (!file) {
        std::cerr << "Error opening file: " << argv[1] << std::endl;
        return 1;
    }
    if (!file.read(reinterpret_cast<char*>(block.data()), BLOCK_SIZE)) {
        std::cerr << "Error reading file" << std::endl;
        return 1;
    }

    // 计算超特征
    int label = finesse.request(block.data());
    
    // 输出超特征
    std::cout << "Computed super feature label: " << label << std::endl;

    return 0;
}
