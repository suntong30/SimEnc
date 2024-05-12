#include <iostream>
#include <vector>
#include <set>
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

	unsigned long long total = 0;
	f.time_check_start();
	for (int i = 0; i < f.N; ++i) {
		RECIPE r;

		XXH64_hash_t h = XXH64(f.trace[i], BLOCK_SIZE, 0);

		if (dedup.count(h)) { // deduplication
			set_ref(r, dedup[h]);
			set_flag(r, 0b10);
			f.recipe_insert(r);
			continue;
		}

		dedup[h] = i;

		int comp_self = LZ4_compress_default(f.trace[i], compressed, BLOCK_SIZE, 2 * BLOCK_SIZE);
		int dcomp_lsh = INF, dcomp_lsh_ref;
		dcomp_lsh_ref = lsh.request((unsigned char*)f.trace[i]);

		if (dcomp_lsh_ref != -1) {
			dcomp_lsh = xdelta3_compress(f.trace[i], BLOCK_SIZE, f.trace[dcomp_lsh_ref], BLOCK_SIZE, delta_compressed, 1);
		}

		set_offset(r, total);

		if (min(comp_self, BLOCK_SIZE) > dcomp_lsh) { // delta compress
			set_size(r, (unsigned long)(dcomp_lsh - 1));
			set_ref(r, dcomp_lsh_ref);
			set_flag(r, 0b11);
			f.write_file(delta_compressed, dcomp_lsh);
			total += dcomp_lsh;
		}
		else {
			if (comp_self < BLOCK_SIZE) { // self compress
				set_size(r, (unsigned long)(comp_self - 1));
				set_flag(r, 0b01);
				f.write_file(compressed, comp_self);
				total += comp_self;
			}
			else { // no compress
				set_flag(r, 0b00);
				f.write_file(f.trace[i], BLOCK_SIZE);
				total += BLOCK_SIZE;
			}
		}
		lsh.insert(i);
		f.recipe_insert(r);
	}
	f.recipe_write();
	cout << "Total time: " << f.time_check_end() << "us\n";

	printf("Trace: %s\n", argv[1]);
	printf("LSH: Finesse, W = %d, SF = %d, feature = %d\n", W, SF_NUM, FEATURE_NUM);
	printf("Final size: %llu (%.2lf%%)\n", total, (double)total * 100 / f.N / BLOCK_SIZE);
}
