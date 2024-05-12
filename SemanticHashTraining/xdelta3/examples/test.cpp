#include <iostream>
#include <vector>
#include <set>
#include <map>
#include <cmath>
#include <algorithm>
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

const int m[12] = {999999761, 999999797, 999999883, 999999893, 999999929, 999999937,
1000000007, 1000000009, 1000000021, 1000000033, 1000000087, 1000000093};
const int a[12] = {1000099, 1000081, 1000039, 1000037, 1000033, 1000003, 999983, 999979, 999961, 999959, 999953, 999931};

size_t sf[3];

static int A = 37,  MOD = 1000000007;
static long long Apower = 1;

void superfeature(char* ptr) {
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
		fp -= (ptr[i] * Apower) % MOD;
		while (fp < 0) fp += MOD;
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

char outputFileName[100];
char recipeName[100];

int main(int argc, char* argv[]) {
	for (int i = 0; i < W - 1; ++i) {
		Apower *= A;
		Apower %= MOD;
	}

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

	sprintf(outputFileName, "%s_output", argv[1]);
	sprintf(recipeName, "%s_recipe", argv[1]);

	vector<RECIPE> recipe;
	map<size_t, int> dedup;

	long long total = 0;
	map<size_t, vector<int>> sfTable[3];
	map<int, int> count;
	f = fopen(outputFileName, "wb");
	for (int i = 0; i < N; ++i) {
		RECIPE r = {};
		recipe.push_back(r);

		string s;
		for (int j = 0; j < BLOCK_SIZE; ++j) s += trace[i][j];
		size_t hashed = hash<string>{}(s);

		if (dedup.count(hashed)) {
			recipe[i].ref = dedup[hashed];
			recipe[i].flag = 2;
			continue;
		}
		dedup[hashed] = i;

		superfeature(trace[i]);

		int comp = LZ4_compress_default(trace[i], out, BLOCK_SIZE, 2 * BLOCK_SIZE);
		int cnt = 0;
		map<int, int> candidate;
		for (int j = 0; j < 3; ++j) {
			for (int u: sfTable[j][sf[j]]) {
				candidate[u]++;
			}
			sfTable[j][sf[j]].push_back(i);
		}

		vector<ii> sorted;
		for (auto it = candidate.begin(); it != candidate.end(); ++it) {
			sorted.push_back({it->second, it->first});
		}
		sort(sorted.begin(), sorted.end());

		int xdeltamin = INF;
		int ref;
		if (sorted.size()) {
			int now = code(1, trace[i], BLOCK_SIZE, trace[sorted.back().second], BLOCK_SIZE, out);
			if (now < xdeltamin) {
				xdeltamin = now;
				ref = sorted.back().second;
				memcpy(minout, out, now);
			}
		}

		count[cnt]++;
		recipe[i].pos = total;

		if (comp <= xdeltamin) {
			int comp = LZ4_compress_default(trace[i], out, BLOCK_SIZE, 2 * BLOCK_SIZE);
			if (comp >= BLOCK_SIZE) {
				recipe[i].ref = (unsigned int)(BLOCK_SIZE - 1) << 20;
				recipe[i].flag = 3;
				fwrite(trace[i], BLOCK_SIZE, 1, f);
				total += BLOCK_SIZE;
			}
			else {
				recipe[i].ref = (unsigned int)(comp - 1) << 20;
				recipe[i].flag = 1;
				fwrite(out, comp, 1, f);
				total += comp;
			}
		}
		else {
			if (xdeltamin >= BLOCK_SIZE) {
				recipe[i].ref = (unsigned int)(BLOCK_SIZE - 1) << 20;
				recipe[i].flag = 3;
				fwrite(trace[i], BLOCK_SIZE, 1, f);
				total += BLOCK_SIZE;
			}
			else {
				recipe[i].ref = (unsigned int)(xdeltamin - 1) << 20;
				recipe[i].ref |= (unsigned int)ref;
				recipe[i].flag = 0;
				fwrite(minout, xdeltamin, 1, f);
				total += xdeltamin;
			}
		}
	}
	fclose(f);

	f = fopen(recipeName, "wb");
	for (int i = 0; i < N; ++i) {
		fwrite(&recipe[i], 1, sizeof(RECIPE), f);
	}
	fclose(f);

	cout << total << '\n';
	cout << (double)total / (4096LL * N) * 100 << '\n';

//	for (auto it = count.begin(); it != count.end(); ++it) {
//		cout << it->first << ": " << it->second << '\n';
//	}

	for (int i = 0; i < N; ++i) {
		free(trace[i]);
	}
}
