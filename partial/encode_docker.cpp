#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#include "zlib.h"
//#include "./zutil.h"
// #include "./zfunc.h"
#include<fcntl.h>
#include<sys/file.h>
#if defined(MSDOS) || defined(OS2) || defined(WIN32) || defined(__CYGWIN__)
#  include <fcntl.h>
#  include <io.h>
#  define SET_BINARY_MODE(file) setmode(fileno(file), O_BINARY)
#else
#  define SET_BINARY_MODE(file)
#endif

#define CURCHUNK 16384

typedef int (*funcp_de)(z_streamp strm, int flush);
extern "C"{
    int ZEXPORT j_deflateInit2_(z_streamp strm, int level, int method,int  windowBits,int  memLevel, int strategy);
    int ZEXPORT j_deflateEnd(z_streamp strm);
    int ZEXPORT j_deflate OF((z_streamp strm, int flush));

}


/* compress or decompress from stdin to stdout */

void copyBytes(FILE* input, FILE* output, int length) {
    unsigned char buffer[CURCHUNK]; // 创建缓冲区
    while (length > 0) {
        int bytesToRead = length < CURCHUNK ? length : CURCHUNK;
        fread(buffer, sizeof(unsigned char), bytesToRead, input); // 从输入文件中读取字节到缓冲区
        fwrite(buffer, sizeof(unsigned char), bytesToRead, output); // 将缓冲区中的字节写入到输出文件中
        fflush(output);
        length -= bytesToRead; // 更新剩余要输出的字节数
    }
}

int compress_data(FILE *source, FILE *dest, int needProcess,int type,int start)
{

    fseek(source,start,SEEK_SET);
    int _need = needProcess;
    int ret, flush;
    unsigned have;
    z_stream strm;
    unsigned char in  [CURCHUNK];
    unsigned char out [CURCHUNK];
    funcp_de hufde;
    int allwrite = 0;
    /* allocate deflate state */
    memset(&strm,0, sizeof(strm));
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    int windowbits = type == 0 ? -15 : 15;
    ret = j_deflateInit2_ (&strm, 9, Z_DEFLATED, windowbits, 8, 4 );
    hufde = j_deflate;   

    if (ret != Z_OK)
        return ret;

    do { // 每次循环重置avail_in，即刷新输入
     
            int curRead = needProcess < CURCHUNK ? needProcess : CURCHUNK;
            strm.avail_in = fread(in, 1, curRead, source);
            needProcess -= curRead; 
            flush = needProcess <= 0 ? Z_FINISH : Z_NO_FLUSH; // source读完，flush变为FINISH；
            strm.next_in = in;
       
        do {  // 每次循环重置avail_out，即刷新输出
            strm.avail_out = CURCHUNK; // CHUNK为每次读取的大小，avail_out为压缩剩余可读取大小
            strm.next_out = out; // out为读取文件的缓存，最大大小为CHUNK，next_out为压缩流读取位置

            ret = j_deflate(&strm, flush);    /* no bad return value */
            assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
            switch (ret) {
                case Z_NEED_DICT:
                    ret = Z_DATA_ERROR;     /* and fall through */
                case Z_DATA_ERROR:
                case Z_MEM_ERROR:
                    (void)j_deflateEnd(&strm);
                    
                    printf("%s\n",strm.msg);
                    return ret;
            }
            have = CURCHUNK - strm.avail_out;
            allwrite += have;
            if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
                (void)j_deflateEnd(&strm);

                return Z_ERRNO;
            }
        } while (strm.avail_out == 0);
        //strm.avail_out == 0
        // assert(strm.avail_in == 0);     /* all input will be used */
        // if(strm.avail_in != 0){
        //     printf("avil in is %d have is : %d\n",strm.avail_in,have);
        // }
        /* done when last data in file processed */
        
    } while (flush != Z_FINISH  );
    assert(ret == Z_STREAM_END);        /* stream will be complete */
    if(allwrite == 0){
        printf("!!!!!!!wrong!!!!!\n");
    }
    
    return Z_OK;
}
bool j_compress_fri(const char * frifile, const char* apk  ,int type)
{
    FILE * blob = fopen(apk, "ab");
    FILE * friBlob = fopen(frifile, "rb");
    int pos ;
    fread(&pos, 1, sizeof(int),friBlob);
    //从friblob输出len个字节到Blob
    copyBytes(friBlob,blob,pos);
    int curPos = pos + sizeof(int);
    int ret ;
    

    //查看frifile_size , deflate数据大小 = frifile_size - pos - crc校验（8位）
    fseek(friBlob,0,SEEK_END);
    long frifile_size = ftell(friBlob);
    fseek(friBlob,curPos,SEEK_SET);

    long length = frifile_size - curPos - 8;
    ret = compress_data(friBlob,blob, length ,type ,curPos);
    if(ret != 0 ){
        printf("ret is %d ,wrong\n",ret);
        return false;
    }

    curPos = curPos + length ;
    fseek(friBlob,curPos,SEEK_SET);
    
    //找到文件结尾，copy剩余的字符
    
    copyBytes(friBlob,blob,8);
    
    int finalSize = ftell(blob);

    fclose(blob);
    fclose(friBlob);
    printf("docker_decode :run %s \n finish with %d bytes\n","success",finalSize);
    return true;
}
int main(int argc, char * argv[]) {
    const char *filename = argv[1]; // 替换为Gzip文件名

    if(!j_compress_fri(filename,argv[2],0)){
        remove(argv[2]);
        j_compress_fri(filename,argv[2],1);
    };

    return 0;
}
