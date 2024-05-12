#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include "zlib.h"
#include "zconf.h"
// #include "./zfunc.h"

#include <vector>
#if defined(MSDOS) || defined(OS2) || defined(WIN32) || defined(__CYGWIN__)
#  include <fcntl.h>
#  include <io.h>
#  define SET_BINARY_MODE(file) setmode(fileno(file), O_BINARY)
#else
#  define SET_BINARY_MODE(file)
#endif
#define CHUNK 16384

extern "C"{
    int j_inflateInit2_(z_streamp strm, int windowBits, const char * version, int stream_size);
    int  j_inflateEnd(z_streamp strm);
    int j_inflate(z_streamp strm, int flush);

}
// Gzip文件头的长度
#define GZIP_HEADER_LEN 10

// 标志位
#define FTEXT    0x01
#define FHCRC    0x02
#define FEXTRA   0x04
#define FNAME    0x08
#define FCOMMENT 0x10

// 解析Gzip文件
void parseGzipFile(const char *filename , long * _offset , long * _len) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        printf("无法打开文件 %s\n", filename);
        return;
    }

    uint8_t header[GZIP_HEADER_LEN];
    size_t bytesRead = fread(header, 1, GZIP_HEADER_LEN, file);
    if (bytesRead != GZIP_HEADER_LEN) {
        printf("无法读取Gzip文件头\n");
        fclose(file);
        return;
    }

    if (header[0] != 0x1F || header[1] != 0x8B) {
        printf("不是Gzip文件\n");
        fclose(file);
        return;
    }
    
    uint8_t flags = header[3];
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 10, SEEK_SET);
    int e_len = 0;
    if (flags & FEXTRA) {
        // 跳过额外字段
        uint16_t extraLen;
        fread(&extraLen, 2, 1, file);
        fseek(file, extraLen, SEEK_CUR);
        e_len += extraLen;
    }

    if (flags & FNAME) {
        // 跳过文件名
        while (1) {
            e_len ++;
            char c = fgetc(file);
            if (c == 0) {

                break;
            }
        }
    }

    if (flags & FCOMMENT) {
        // 跳过评论
        while (1) {
            e_len++;
            char c = fgetc(file);
            if (c == 0) break;
        }
    }

    if (flags & FHCRC) {
        // 跳过头部CRC校验
        e_len += 2 ;
        fseek(file, 2, SEEK_CUR);
    }

    fseek(file, GZIP_HEADER_LEN + e_len , SEEK_SET); // 定位到Deflate数据

    *_offset = ftell(file);
    *_len = fileSize - *_offset - 8; // 减去CRC32和ISize字段的长度

    // printf("Deflate数据偏移:%ld\n", offset);
    // printf("Deflate数据长度:%ld\n", length);

    fclose(file);
}





bool  decompressBytes(FILE* input, FILE* output, int windowBits,
                         int compressedStart, int compressedLength ,long * allUncom, int type) {

    unsigned char inBuffer[CHUNK];
    unsigned char outBuffer[CHUNK];

    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    stream.avail_in = 0;
    stream.next_in = Z_NULL;
    j_inflateInit2_(&stream, windowBits,"1.2.11",(int)sizeof(z_stream));
    fseek(input, compressedStart, SEEK_SET);
    //type 为0代表huffman 否则type为zlib解压
    while (compressedLength > 0) {
        int bytesToRead = compressedLength < CHUNK ? compressedLength : CHUNK ;
        stream.avail_in = fread(inBuffer, 1, bytesToRead, input);
        stream.next_in = inBuffer;
        do {
            stream.avail_out = CHUNK;
            stream.next_out = outBuffer;
            int ret = j_inflate(&stream, Z_NO_FLUSH);
            switch (ret) {
            case Z_NEED_DICT:
                ret = Z_DATA_ERROR;     
            case Z_STREAM_ERROR:
            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
                (void)j_inflateEnd(&stream);
                return false;
            }
            int have = CHUNK - stream.avail_out;
            *allUncom += have;
            fwrite(outBuffer, 1, have, output);
        } while (stream.avail_out == 0);
        compressedLength -= bytesToRead;
    }
    //j_inflateEnd(&stream);
    return true;
}

void copyBytes(FILE* input, FILE* output, int length) {
    char buffer[CHUNK]; // 创建缓冲区
    while (length > 0) {
        int bytesToRead = length < CHUNK ? length : CHUNK;
        fread(buffer, sizeof(char), bytesToRead, input); // 从输入文件中读取字节到缓冲区
        fwrite(buffer, sizeof(char), bytesToRead, output); // 将缓冲区中的字节写入到输出文件中
        length -= bytesToRead; // 更新剩余要输出的字节数
    }
}

bool j_build_fri(const char* apk,const char * frifile ,
                int pos, long length,int count,int winbit)
{
    FILE * blob = fopen(apk, "rb");
    FILE * friBlob = fopen(frifile, "ab");
    fwrite(&pos,1,sizeof(int),friBlob);
    int curPos = 0;

    bool flag = true;
    long process = 0, inf_process = 0;
    copyBytes(blob,friBlob,pos);
    //解压缩length[i]个字节到friBlob
    long friBlobPos = ftell(friBlob);
    long curUncom = 0 ;
        //正常zlib 以及 huffman 0 都使用windowBIts = -15
    if(!decompressBytes( blob, friBlob, winbit, pos, length, &curUncom,  0)){
        flag  = false;
    }
        
    curPos = curPos + length + pos;
    process += length;
    inf_process += curUncom;
    
    // fclose(fp);
    //找到文件结尾，copy剩余的字符
    printf("preocess : deflate-%ld ->  Huffman-%ld\n",process,inf_process);
    fseek(blob, 0 , SEEK_END);
    long  fileSize = ftell(blob);
    fseek(blob,curPos,SEEK_SET);
    if(fileSize > curPos && flag){
        int len = fileSize - curPos;
        copyBytes(blob,friBlob,len);
    }
    int firSize = ftell(friBlob);
    fclose(blob);
    fclose(friBlob);
    printf("docker_decode :run partial %s \nfinish with %d bytes\n",flag?"success":"wrong",firSize);
    return flag;
}

int main(int argc, char * argv[]) {
    const char *filename = argv[1]; // 替换为Gzip文件名
    long offset,length;
    parseGzipFile(filename,&offset,&length);
    printf("Deflate数据偏移:%ld\n", offset);
    printf("Deflate数据长度:%ld\n", length);
    if(!j_build_fri(filename,argv[2],offset,length,1,-15)){
        remove(argv[2]);
        j_build_fri(filename,argv[2],offset,length,1, 15);
    };

    return 0;
}
