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
#include "deepsketch.h"
#include "../lz4.h"
#include "../xxhash.h"
extern "C" {
	#include "../xdelta3/xdelta3.h"
}
#define INF 987654321
using namespace std;

typedef pair<int, int> ii;

int main(int argc, char* argv[]) {
	if (argc != 7) {
		cerr << "usage: ./combined_inf [script_module] [input_file] [query_num] [window_size] [SF_NUM] [FEATURE_NUM]\n";
		exit(0);
	}
	int query_num = atoi(argv[3]);
	int W = atoi(argv[4]);
	int SF_NUM = atoi(argv[5]);
	int FEATURE_NUM = atoi(argv[6]);

	DATA_IO f(argv[2]);
	f.read_file();

	map<XXH64_hash_t, int> dedup;
	list<ii> dedup_lazy_recipe;

	LSH lsh(BLOCK_SIZE, W, SF_NUM, FEATURE_NUM); // parameter
	NetworkHash network(256, argv[1]);

	string indexPath = "ngtindex";
	NGT::Property property;
	property.dimension = HASH_SIZE / 8;
	property.objectType = NGT::ObjectSpace::ObjectType::Uint8;
	property.distanceType = NGT::Index::Property::DistanceType::DistanceTypeHamming;
	NGT::Index::create(indexPath, property);
	NGT::Index index(indexPath);

	ANN ann(query_num, 128, 16, 128, &property, &index);

	unsigned long long total[10] = {};
	vector<tuple<int, int, int>> log[10];

	for (int i = 0; i < f.N; ++i) {
		XXH64_hash_t h = XXH64(f.trace[i], BLOCK_SIZE, 0);

		if (dedup.count(h)) { // deduplication
			dedup_lazy_recipe.push_back({i, dedup[h]});
			continue;
		}

		dedup[h] = i;

		if (network.push(f.trace[i], i)) {
			vector<pair<MYHASH, int>> myhash = network.request();
			for (int j = 0; j < myhash.size(); ++j) {
				while (!dedup_lazy_recipe.empty() && dedup_lazy_recipe.begin()->first < index) {
					for (int k = 0; k < 20; ++k) {
						log[k].push_back({2, 0, dedup_lazy_recipe.begin()->second});
					}
					dedup_lazy_recipe.pop_front();
				}

				RECIPE r;

				MYHASH& h = myhash[j].first;
				int index = myhash[j].second;

				int comp_self = LZ4_compress_default(f.trace[index], compressed, BLOCK_SIZE, 2 * BLOCK_SIZE);
				int dcomp_lsh = INF, dcomp_lsh_ref;
				vector<int> dcomp_lsh_cand = lsh.request((unsigned char*)f.trace[index]);

				int dcomp_ann = INF, dcomp_ann_ref;
				vector<int> dcomp_ann_cand = ann.request(h);

				for (int k = 0; k < 10; ++k) {
					if (k < dcomp_lsh_cand.size()) {
						int now = xdelta3_compress(f.trace[index], BLOCK_SIZE, f.trace[dcomp_lsh_ref], BLOCK_SIZE, delta_compressed, 1);
						if (now < dcomp_lsh) {
							dcomp_lsh = now;
							dcomp_lsh_ref = dcomp_lsh_cand[k];
						}
					}

					if (k < dcomp_ann_cand.size()) {
						int now = xdelta3_compress(f.trace[index], BLOCK_SIZE, f.trace[dcomp_ann_ref], BLOCK_SIZE, delta_compressed, 1);
						if (now < dcomp_ann) {
							dcomp_ann = now;
							dcomp_ann_ref = dcomp_ann_cand[k];
						}
					}

					if (min(comp_self, BLOCK_SIZE) > min(dcomp_ann, dcomp_lsh)) { // delta compress
						if (dcomp_lsh > dcomp_ann) {
							total[k] += dcomp_ann;
							log[k].push_back({3, dcomp_ann, dcomp_ann_ref});
						}
						else {
							total[k] += dcomp_lsh;
							log[k].push_back({3, dcomp_lsh, dcomp_lsh_ref});
						}
					}
					else {
						if (comp_self < BLOCK_SIZE) { // self compress
							total[k] += comp_self;
							log[k].push_back({1, comp_self, 0});
						}
						else { // no compress
							total[k] += BLOCK_SIZE;
							log[k].push_back({0, BLOCK_SIZE, 0});
						}
					}
#ifdef PRINT_HASH
					cout << index << ' ' << h << '\n';
#endif
				}

				lsh.insert(index);
				ann.insert(h, index);
			}
		}
	}
	// LAST REQUEST
	{
		vector<pair<MYHASH, int>> myhash = network.request();
		for (int j = 0; j < myhash.size(); ++j) {
			RECIPE r;

			MYHASH& h = myhash[j].first;
			int index = myhash[j].second;

			int comp_self = LZ4_compress_default(f.trace[index], compressed, BLOCK_SIZE, 2 * BLOCK_SIZE);
			int dcomp_lsh = INF, dcomp_lsh_ref;
			vector<int> dcomp_lsh_cand = lsh.request((unsigned char*)f.trace[index]);

			int dcomp_ann = INF, dcomp_ann_ref;
			vector<int> dcomp_ann_cand = ann.request(h);

			for (int k = 0; k < 10; ++k) {
				if (k < dcomp_lsh_cand.size()) {
					int now = xdelta3_compress(f.trace[index], BLOCK_SIZE, f.trace[dcomp_lsh_ref], BLOCK_SIZE, delta_compressed, 1);
					if (now < dcomp_lsh) {
						dcomp_lsh = now;
						dcomp_lsh_ref = dcomp_lsh_cand[k];
					}
				}

				if (k < dcomp_ann_cand.size()) {
					int now = xdelta3_compress(f.trace[index], BLOCK_SIZE, f.trace[dcomp_ann_ref], BLOCK_SIZE, delta_compressed, 1);
					if (now < dcomp_ann) {
						dcomp_ann = now;
						dcomp_ann_ref = dcomp_ann_cand[k];
					}
				}

				if (min(comp_self, BLOCK_SIZE) > min(dcomp_ann, dcomp_lsh)) { // delta compress
					if (dcomp_lsh > dcomp_ann) {
						total[k] += dcomp_ann;
						log[k].push_back({3, dcomp_ann, dcomp_ann_ref});
					}
					else {
						total[k] += dcomp_lsh;
						log[k].push_back({3, dcomp_lsh, dcomp_lsh_ref});
					}
				}
				else {
					if (comp_self < BLOCK_SIZE) { // self compress
						total[k] += comp_self;
						log[k].push_back({1, comp_self, 0});
					}
					else { // no compress
						total[k] += BLOCK_SIZE;
						log[k].push_back({0, BLOCK_SIZE, 0});
					}
				}
#ifdef PRINT_HASH
				cout << index << ' ' << h << '\n';
#endif
			}

			lsh.insert(index);
			ann.insert(h, index);

			while (!dedup_lazy_recipe.empty() && dedup_lazy_recipe.begin()->first < index) {
				for (int k = 0; k < 20; ++k) {
					log[k].push_back({2, 0, dedup_lazy_recipe.begin()->second});
				}
				dedup_lazy_recipe.pop_front();
			}
		}
	}
	for (int i = 0; i < 10; ++i) {
		char name[1000];
		sprintf(name, "Finesse+ANN_%s_%d", argv[1], i + 1);

		FILE* out = fopen(name, "wt");
		fprintf(out, "Trace: %s\n", argv[1]);
		fprintf(out, "LSH: Finesse, W = %d, SF = %d, feature = %d\n", W, SF_NUM, FEATURE_NUM);
		fprintf(out, "ANN: %s\n", argv[2]);
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
