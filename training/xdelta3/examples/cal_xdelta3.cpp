#include <iostream>
#include <vector>
#include <string>
#include <set>
#include "../xdelta3.h"
#include "../xdelta3.c"
#include "../lz4.h"
#define BLOCK_SIZE 4096
using namespace std;

int code (int encode, char* InFile, int InSize, char* SrcFile, int SrcSize, char* OutFile) {
	int BufSize = 0x1000;
	int OutSize = 0;

	int r, ret;
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

char buf[BLOCK_SIZE];
char out[0x2000];
char out2[0x2000];
int N = 0;
vector<char*> trace;

int do_xdelta3(int i, int j) {
	return code(1, trace[i], BLOCK_SIZE, trace[j], BLOCK_SIZE, out);
}

int main(int argc, char* argv[]) {
	FILE* f = fopen(argv[1], "rb");
	while (1) {
		char* ptr = (char*)malloc(BLOCK_SIZE);
		trace.push_back(ptr);
		int now = fread(trace[N++], 1, BLOCK_SIZE, f);
		if (!now) {
			N--;
			break;
		}
	}
	fclose(f);

	long long total = 0;
	int total_block = 0;
	int num_cluster = 0;
	int block_in_cluster = 0;
	int sz;
	while (cin >> sz) {
		total_block += sz;
		if (sz >= 2) {
			num_cluster++;
			block_in_cluster += sz;
		}
		int rep, t;
		cin >> rep;
		total += LZ4_compress_default(trace[rep], out, BLOCK_SIZE, 2 * BLOCK_SIZE);
		for (int i = 1; i < sz; ++i) {
			cin >> t;
			total += do_xdelta3(rep, t);
		}
	}
	printf("Total blocks: %d\n", total_block);
	printf("Number of cluster: %d\n", num_cluster);
	printf("Covered Block: %d (%.2lf%%)\n", block_in_cluster, 100.0 * block_in_cluster / total_block);
	printf("Compressed size: %lld\n", total);
	printf("Ratio: %.2lf%%\n", 100.0 * total / BLOCK_SIZE / N);
}
