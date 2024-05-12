#include <iostream>
#include <vector>
#include <set>
#include <tuple>
#include <bitset>
#include <map>
#include <cmath>
#include <algorithm>
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
	if (argc != 4) {
		cerr << "usage: ./ann_inf [input_file] [script_module] [threshold]\n";
		exit(0);
	}
	int threshold = atoi(argv[3]);

	DATA_IO f(argv[1]);
	f.read_file();

	map<XXH64_hash_t, int> dedup;
	list<ii> dedup_lazy_recipe;
	NetworkHash network(256, argv[2]);

	string indexPath = "ngtindex";
	NGT::Property property;
	property.dimension = HASH_SIZE / 8;
	property.objectType = NGT::ObjectSpace::ObjectType::Uint8;
	property.distanceType = NGT::Index::Property::DistanceType::DistanceTypeHamming;
	NGT::Index::create(indexPath, property);
	NGT::Index index(indexPath);

	ANN ann(20, 128, 16, threshold, &property, &index);

	unsigned long long total[20] = {};
	vector<tuple<int, int, int>> log[20];

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
				int dcomp_ann = INF, dcomp_ann_ref;
				vector<int> dcomp_ann_cand = ann.request(h);

				for (int k = 0; k < 20; ++k) {
					if (k < dcomp_ann_cand.size()) {
						int now = xdelta3_compress(f.trace[index], BLOCK_SIZE, f.trace[dcomp_ann_ref], BLOCK_SIZE, delta_compressed, 1);
						if (now < dcomp_ann) {
							dcomp_ann = now;
							dcomp_ann_ref = dcomp_ann_cand[k];
						}
					}

					if (min(comp_self, BLOCK_SIZE) > dcomp_ann) { // delta compress
						total[k] += dcomp_ann;
						log[k].push_back({3, dcomp_ann, dcomp_ann_ref});
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
			int dcomp_ann = INF, dcomp_ann_ref;
			vector<int> dcomp_ann_cand = ann.request(h);

			for (int k = 0; k < 20; ++k) {
				if (k < dcomp_ann_cand.size()) {
					int now = xdelta3_compress(f.trace[index], BLOCK_SIZE, f.trace[dcomp_ann_ref], BLOCK_SIZE, delta_compressed, 1);
					if (now < dcomp_ann) {
						dcomp_ann = now;
						dcomp_ann_ref = dcomp_ann_cand[k];
					}
				}

				if (min(comp_self, BLOCK_SIZE) > dcomp_ann) { // delta compress
					total[k] += dcomp_ann;
					log[k].push_back({3, dcomp_ann, dcomp_ann_ref});
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

			ann.insert(h, index);

			while (!dedup_lazy_recipe.empty() && dedup_lazy_recipe.begin()->first < index) {
				for (int k = 0; k < 20; ++k) {
					log[k].push_back({2, 0, dedup_lazy_recipe.begin()->second});
				}
				dedup_lazy_recipe.pop_front();
			}
		}
	}
	for (int i = 0; i < 20; ++i) {
		char name[1000];
		sprintf(name, "ANN_%s_%d", argv[1], i + 1);

		FILE* out = fopen(name, "wt");
		fprintf(out, "Trace: %s\n", argv[1]);
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
