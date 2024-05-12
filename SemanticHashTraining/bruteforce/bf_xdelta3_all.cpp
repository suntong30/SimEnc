#include <iostream>
#include <vector>
#include <tuple>
#include <set>
#include <algorithm>
#include "../xdelta3/xdelta3.h"
#include "../xxhash.h"
#include "../lz4.h"
#define BLOCK_SIZE 4096
#define MAX_THREAD 256
#define INF 987654321
using namespace std;

vector<char*> trace;
vector<bool> unique_block;
int N;

char buf[MAX_THREAD][2 * BLOCK_SIZE];
char out[MAX_THREAD][2 * BLOCK_SIZE];

char file_name_temp[100];
char file_name_result[100];
FILE* fp_temp;

typedef tuple<int, int, int> i3;

set<int> todo;
pthread_mutex_t mutex;
vector<i3> result;

void read_file(char* name) {
	FILE* f = fopen(name, "rb");
	while (1) {
        char* ptr = new char[BLOCK_SIZE];
		trace.push_back(ptr);
        int now = fread(trace[N++], 1, BLOCK_SIZE, f);
		if (!now) {
            delete[] trace.back();
			trace.pop_back();
			N--;
			break;
		}
	}
	fclose(f);
}

void restore_result(char* name) {
	FILE* f = fopen(file_name_temp, "rt");
	if (f == NULL) return;

	int num, ref, size;
	while (fscanf(f, "%d %d %d", &num, &ref, &size) == 3) {
		result.push_back({num, ref, size});
		todo.erase(num);
	}
	fclose(f);
}

void print_result(char* name) {
	long long total = 0;
	sort(result.begin(), result.end());

	for (i3 u: result) {
		total += get<2>(u);
	}

	FILE* f = fopen(file_name_result, "wt");
	fprintf(f, "%llu %.2lf\n", total, (double)total / N / BLOCK_SIZE * 100);
	for (i3 u: result) {
		fprintf(f, "%d %d %d\n", get<0>(u), get<1>(u), get<2>(u));
	}
	fclose(f);
}

void* func(void* arg) {
	int id = (long long)arg;

	while (1) {
		pthread_mutex_lock(&mutex);
		if (todo.empty()) {
			pthread_mutex_unlock(&mutex);
			break;
		}
		int i = *todo.begin();
		todo.erase(i);
		pthread_mutex_unlock(&mutex);

		int size = LZ4_compress_default(trace[i], out[id], BLOCK_SIZE, 2 * BLOCK_SIZE);
		int ref = -1;

		for (int j = 0; j < i; ++j) {
			if (!unique_block[j]) continue;
    		int now = xdelta3_compress(trace[i], 4096, trace[j], 4096, out[id], 1);
			if (now < size) {
				size = now;
				ref = j;
			}
		}

		pthread_mutex_lock(&mutex);
		result.push_back({i, ref, size});
		fprintf(fp_temp, "%d %d %d\n", i, ref, size);
		if (i % 100 == 0) {
			fprintf(stderr, "%d/%d\r", i, N);
		}
		pthread_mutex_unlock(&mutex);
	}
	return NULL;
}

int main(int argc, char* argv[]) {
	if (argc != 3) {
		printf("usage: ./bf [file_name] [num_thread]\n");
		exit(0);
	}
	sprintf(file_name_temp, "%s_bf_temp", argv[1]);
	sprintf(file_name_result, "%s_bf_result", argv[1]);

	int NUM_THREAD = atoi(argv[2]);

    read_file(argv[1]);
	unique_block.resize(N, 0);

    set<XXH64_hash_t> dedup;
	for (int i = 0; i < N; ++i) {
		XXH64_hash_t h = XXH64(trace[i], BLOCK_SIZE, 0);
		if (!dedup.count(h)) {
			todo.insert(i);
			dedup.insert(h);
			unique_block[i] = 1;
		}
	}

    restore_result(argv[1]);

	pthread_t tid[MAX_THREAD];
	pthread_mutex_init(&mutex, NULL);
	fp_temp = fopen(file_name_temp, "at");

	for (int i = 0; i < NUM_THREAD; ++i) {
		pthread_create(&tid[i], NULL, func, (void*)i);
	}

	for (int i = 0; i < NUM_THREAD; ++i) {
		pthread_join(tid[i], NULL);
	}
	fclose(fp_temp);

	print_result(argv[1]);
}