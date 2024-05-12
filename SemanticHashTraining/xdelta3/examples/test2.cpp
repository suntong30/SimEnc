#include <iostream>
#include <vector>
#include <set>
#include <map>
#include <cmath>
#include "../../lz4/lib/lz4.h"
#include "../xdelta3.h"
#include "../xdelta3.c"
#define W 32
#define INF 987654321
#define BLOCK_SIZE 4096
using namespace std;

typedef long long ll;
typedef pair<int, int> ii;

typedef struct RECIPE {
	unsigned int pos;
	unsigned int ref;
	unsigned char flag;
} RECIPE;

int code (int encode, char* InFile, int InSize, char* SrcFile, int SrcSize, char* OutFile) {
	int BufSize = 0x1000;
	int OutSize = 0;

	int ret;
	xd3_stream stream;
	xd3_config config;
	xd3_source source;
	void* Input_Buf;
	int Input_Buf_Read;

	if (BufSize < XD3_ALLOCSIZE)
		BufSize = XD3_ALLOCSIZE;

	memset (&stream, 0, sizeof (stream));
	memset (&source, 0, sizeof (source));

	xd3_init_config(&config, XD3_ADLER32);
	config.winsize = BufSize;
	xd3_config_stream(&stream, &config);

	source.blksize = BufSize;
	source.curblk = (const unsigned char*)malloc(source.blksize);

	/* Load 1st block of stream. */
	source.onblk = min(source.blksize, SrcSize);
	memcpy((void*)source.curblk, SrcFile, source.onblk);
	SrcSize -= source.onblk;
	source.curblkno = 0;
	/* Set the stream. */
	xd3_set_source(&stream, &source);

	Input_Buf = malloc(BufSize);

	do
	{
		Input_Buf_Read = min(BufSize, InSize);
		memcpy(Input_Buf, InFile, Input_Buf_Read);
		if (Input_Buf_Read < BufSize)
		{
			xd3_set_flags(&stream, XD3_FLUSH | stream.flags);
		}
		xd3_avail_input(&stream, (const unsigned char*)Input_Buf, Input_Buf_Read);

process:
		if (encode)
			ret = xd3_encode_input(&stream);
		else
			ret = xd3_decode_input(&stream);

		switch (ret)
		{
			case XD3_INPUT:
				{
					continue;
				}

			case XD3_OUTPUT:
				{
					memcpy(OutFile + OutSize, stream.next_out, stream.avail_out);
					OutSize += stream.avail_out;
					xd3_consume_output(&stream);
					goto process;
				}

			case XD3_GETSRCBLK:
				{
					source.onblk = min(source.blksize, SrcSize);
					memcpy((void*)source.curblk, SrcFile + source.blksize * source.getblkno, source.onblk);
					SrcSize -= source.onblk;
					source.curblkno = source.getblkno;
					goto process;
				}

			case XD3_GOTHEADER:
				{
					goto process;
				}

			case XD3_WINSTART:
				{
					goto process;
				}

			case XD3_WINFINISH:
				{
					goto process;
				}

			default:
				{
					return ret;
				}

		}

	}
	while (Input_Buf_Read == BufSize);

	free(Input_Buf);

	free((void*)source.curblk);
	xd3_close_stream(&stream);
	xd3_free_stream(&stream);

	return OutSize;

};

int N;
vector<char*> trace;
char buf[BLOCK_SIZE];
char out[2 * BLOCK_SIZE];
char minout[2 * BLOCK_SIZE];

char outputFileName[100];
char recipeName[100];
char restoreFileName[100];

int main(int argc, char* argv[]) {
	sprintf(outputFileName, "%s_output", argv[1]);
	sprintf(recipeName, "%s_recipe", argv[1]);
	sprintf(restoreFileName, "%s_restore", argv[1]);

	vector<RECIPE> recipe;

	FILE* f = fopen(recipeName, "rb");
	while (1) {
		RECIPE r;
		int rd = fread(&r, sizeof(RECIPE), 1, f);
		if (rd == 0) break;
		recipe.push_back(r);
		N++;
	}
	fclose(f);

	f = fopen(outputFileName, "rb");
	FILE* fout = fopen(restoreFileName, "wb");

	for (int i = 0; i < N; ++i) {
		char* ptr = (char*)malloc(BLOCK_SIZE);

		if (recipe[i].flag == 0) {
			int ref = recipe[i].ref & ((1 << 20) - 1);
			int sz = (recipe[i].ref >> 20) + 1;
			fread(buf, sz, 1, f);
			int outsz = code(0, buf, sz, trace[ref], BLOCK_SIZE, ptr);
			if (outsz != BLOCK_SIZE) {
				printf("Xdelta Error on %d\n", i);
				break;
			}
		}
		else if (recipe[i].flag == 1) {
			int sz = (recipe[i].ref >> 20) + 1;
			fread(buf, sz, 1, f);
			int outsz = LZ4_decompress_safe(buf, ptr, sz, BLOCK_SIZE);
			if (outsz != BLOCK_SIZE) {
				printf("LZ Error on %d\n", i);
				break;
			}
		}
		else if (recipe[i].flag == 2) {
			int ref = recipe[i].ref & ((1 << 20) - 1);
			memcpy(ptr, trace[ref], BLOCK_SIZE);
		}
		else {
			fread(ptr, BLOCK_SIZE, 1, f);
		}
		fwrite(ptr, BLOCK_SIZE, 1, fout);
		trace.push_back(ptr);
	}
	fclose(f);
	fclose(fout);

	for (int i = 0; i < N; ++i) {
		free(trace[i]);
	}
}
