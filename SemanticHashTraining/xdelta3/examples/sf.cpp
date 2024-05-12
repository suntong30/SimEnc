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

int N;
vector<char*> trace;
char buf[BLOCK_SIZE];
char out[2 * BLOCK_SIZE];
char out2[2 * BLOCK_SIZE];

const int m[12] = {999999761, 999999797, 999999883, 999999893, 999999929, 999999937,
1000000007, 1000000009, 1000000021, 1000000033, 1000000087, 1000000093};
const int a[12] = {1000099, 1000081, 1000039, 1000037, 1000033, 1000003, 999983, 999979, 999961, 999959, 999953, 999931};

size_t sf[3];

void superfeature(char* ptr) {
	static int A = 37,  MOD = 1000000007;

	unsigned int feature[12] = {};
	long long fp = 0;
	for (int i = 0; i < W; ++i) {
		fp *= A;
		fp += (unsigned char)ptr[i];
		fp %= MOD;
	}

	for (int i = 0; i < BLOCK_SIZE - W + 1; ++i) {
		for (int j = 0; j < 12; ++j) {
			long long trans = m[j] * fp + a[j];
			feature[j] = max(feature[j], (unsigned int)(trans & 0xffffffff));
		}
		if (i != BLOCK_SIZE - W) {
			fp *= A;
			fp += ptr[i + W];
			fp %= MOD;
		}
	}

	for (int i = 0; i < 3; ++i) {
		string s;
		for (int j = 0; j < 4; ++j) {
			for (int k = 0; k < 5; ++k) {
				s += feature[4 * i + j] % 128;
				feature[4 * i + j] /= 128;
			}
		}
		sf[i] = hash<string>{}(s);
	}
}

int main(int argc, char* argv[]) {
	FILE* f = fopen(argv[1], "rb");
	while (1) {
		char* ptr = (char*)malloc(BLOCK_SIZE);
		trace.push_back(ptr);
		int now = fread(trace[N++], 1, BLOCK_SIZE, f);
		if (!now) {
			free(trace.back());
			trace.pop_back();
			N--;
			break;
		}
	}
	fclose(f);

	set<size_t> dedup;

	long long total = 0;
	map<size_t, vector<int>> sfTable[3];
	map<int, int> count;
	for (int i = 0; i < N; ++i) {
		string s;
		for (int j = 0; j < BLOCK_SIZE; ++j) s += trace[i][j];
		size_t hashed = hash<string>{}(s);

		if (dedup.count(hashed)) continue;
		dedup.insert(hashed);

		superfeature(trace[i]);

		int self = LZ4_compress_default(trace[i], out, BLOCK_SIZE, 2 * BLOCK_SIZE);
		int xdeltamin = INF, tt;
		int ref;
		int cnt = 0;
		for (int j = 0; j < 3; ++j) {
			for (int u: sfTable[j][sf[j]]) {
				int now = code(1, trace[i], BLOCK_SIZE, trace[u], BLOCK_SIZE, out);
				int now2 = LZ4_compress_default(out, out2, now, 2 * BLOCK_SIZE);
				if (now2 < xdeltamin) {
					xdeltamin = now2;
					tt = now;
					ref = u;
				}
				cnt++;
			}
			sfTable[j][sf[j]].push_back(i);
		}

		count[cnt]++;

		if (self < xdeltamin) {
			total += self;
		}
		else {
			total += xdeltamin;
		}
		cout << self << ' ' << tt << ' ' << xdeltamin << ' ' << total << endl;
	}

	cout << total << '\n';
	cout << (double)total / (4096LL * N) * 100 << '\n';

	for (auto it = count.begin(); it != count.end(); ++it) {
		cout << it->first << ": " << it->second << '\n';
	}

	for (int i = 0; i < N; ++i) {
		free(trace[i]);
	}
}
