#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void concatFiles(const char* folderPath, const char* newFileName) {
    // 打开新文件以写入二进制数据
    FILE* newFile = fopen(newFileName, "wb");
    if (newFile == NULL) {
        printf("无法打开新文件！\n");
        return;
    }

    // 打开文件夹
    char command[256];
    //snprintf(command, sizeof(command), "dir /B \"%s\"", folderPath); // 适用于Windows系统
    snprintf(command, sizeof(command), "ls -p \"%s\"", folderPath); // 适用于Linux系统
    FILE* pipe = popen(command, "r");
    if (pipe == NULL) {
        printf("无法打开文件夹！\n");
        fclose(newFile);
        return;
    }

    // 逐个读取文件并拼接到新文件中
    char fileName[256];
    while (fgets(fileName, sizeof(fileName), pipe) != NULL) {
        // 去除文件名末尾的换行符
        fileName[strcspn(fileName, "\n")] = '\0';

        // 打开当前文件以读取二进制数据
        char filePath[256];
        snprintf(filePath, sizeof(filePath), "%s/%s", folderPath, fileName);
        FILE* currentFile = fopen(filePath, "rb");
        if (currentFile == NULL) {
            printf("无法打开文件：%s\n", fileName);
            continue;
        }

        // 从当前文件读取数据并写入新文件
        int bufferLength = 1024;
        char buffer[bufferLength];
        size_t bytesRead;
        while ((bytesRead = fread(buffer, 1, bufferLength, currentFile)) > 0) {
            fwrite(buffer, 1, bytesRead, newFile);
        }

        fclose(currentFile); // 关闭当前文件
    }

    pclose(pipe); // 关闭文件夹管道
    fclose(newFile); // 关闭新文件

    printf("拼接完成！\n");
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printf("参数不足！\n");
        printf("用法: %s 文件夹路径 新文件名称\n", argv[0]);
        return 1;
    }

    const char* folderPath = argv[1];
    const char* newFileName = argv[2];

    concatFiles(folderPath, newFileName);

    return 0;
}
