#include <iostream>
#include <vector>
#include <set>
#include <tuple>
#include <bitset>
#include <map>
#include <cmath>
#include <algorithm>
#include "finesse.h"
#include "../compress.h"
#include "../lz4.h"
#include "../xxhash.h"
#include "../xdelta3/xdelta3.h"
#define INF 987654321
using namespace std;

int main(int argc, char* argv[]) {
	if (argc != 5) {
		cerr << "usage: ./lsh_inf [input_file] [window_size] [SF_NUM] [FEATURE_NUM]\n";
		exit(0);
	}
	int W = atoi(argv[2]);
	int SF_NUM = atoi(argv[3]);
	int FEATURE_NUM = atoi(argv[4]);

	DATA_IO f(argv[1]);
	f.read_file();

	map<XXH64_hash_t, int> dedup;
	Finesse lsh(BLOCK_SIZE, W, SF_NUM, FEATURE_NUM); // parameter

	unsigned long long total[20] = {};
	vector<tuple<int, int, int>> log[20];

	f.time_check_start();
	for (int i = 0; i < f.N; ++i) {
		RECIPE r;

		XXH64_hash_t h = XXH64(f.trace[i], BLOCK_SIZE, 0);

		if (dedup.count(h)) { // deduplication
			for (int j = 0; j < 20; ++j) {
				log[j].push_back({2, 0, dedup[h]});
			}
			continue;
		}

		dedup[h] = i;

		int comp_self = LZ4_compress_default(f.trace[i], compressed, BLOCK_SIZE, 2 * BLOCK_SIZE);
		int dcomp_lsh = INF, dcomp_lsh_ref;
		vector<int> dcomp_lsh_cand = lsh.request((unsigned char*)f.trace[i]);

		for (int j = 0; j < 20; ++j) {
			if (j < dcomp_lsh_cand.size()) {
				int now = xdelta3_compress(f.trace[i], BLOCK_SIZE, f.trace[dcomp_lsh_cand[j]], BLOCK_SIZE, delta_compressed, 1);
				if (now < dcomp_lsh) {
					dcomp_lsh = now;
					dcomp_lsh_ref = dcomp_lsh_cand[j];
				}
			}

			if (min(comp_self, BLOCK_SIZE) > dcomp_lsh) { // delta compress
				total[j] += dcomp_lsh;
				log[j].push_back({3, dcomp_lsh, dcomp_lsh_ref});
			}
			else {
				if (comp_self < BLOCK_SIZE) { // self compress
					total[j] += comp_self;
					log[j].push_back({1, comp_self, 0});
				}
				else { // no compress
					total[j] += BLOCK_SIZE;
					log[j].push_back({0, BLOCK_SIZE, 0});
				}
			}
		}

		lsh.insert(i);
	}

	for (int i = 0; i < 20; ++i) {
		char name[1000];
		sprintf(name, "Finesse_%s_%d", argv[1], i + 1);

		FILE* out = fopen(name, "wt");
		fprintf(out, "Trace: %s\n", argv[1]);
		fprintf(out, "LSH: Finesse, W = %d, SF = %d, feature = %d\n", W, SF_NUM, FEATURE_NUM);
		fprintf(out, "cand %d\n", i + 1);
		fprintf(out, "Final size: %llu (%.2lf%%)\n", total[i], (double)total[i] * 100 / f.N / BLOCK_SIZE);
		for (int j = 0; j < log[i].size(); ++j) {
			if (get<0>(log[i][j]) == 0) {
				fprintf(out, "0 4096\n");
			}
			else if (get<0>(log[i][j]) == 1) {
				fprintf(out, "1 %d\n", get<1>(log[i][j]));
			}
			else if (get<0>(log[i][j]) == 2) {
				fprintf(out, "2 %d\n", get<2>(log[i][j]));
			}
			else if (get<0>(log[i][j]) == 3) {
				fprintf(out, "3 %d %d\n", get<1>(log[i][j]), get<2>(log[i][j]));
			}
		}
		fclose(out);
	}
}
