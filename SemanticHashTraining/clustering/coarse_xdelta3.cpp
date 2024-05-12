#include <iostream>
#include <vector>
#include <cmath>
#include <set>
#include <map>
#include <queue>
#include <thread>
#include "../xxhash.h"
#include "../xdelta3/xdelta3.h"
// #define BLOCK_SIZE (512*1024)
#define BLOCK_SIZE (4*1024)
#define INF 987654321
// #define COARSE_T (256*1024)
#define COARSE_T (2*1024)
#define MAX_THREAD 256
using namespace std;

int N = 0;
int NUM_THREAD;
vector<char*> trace;
char buf[MAX_THREAD][BLOCK_SIZE];
char compressed[MAX_THREAD][2 * BLOCK_SIZE];



int do_xdelta3(int i, int j, int id) {
	return xdelta3_compress(trace[i], BLOCK_SIZE, trace[j], BLOCK_SIZE, compressed[id], 1);
}

typedef tuple<int, int, int, int> BFinfo;

struct BruteForceClusterArgument {
	pthread_mutex_t* mutex;
	int id;
	vector<int>* todo;
	int* rep_list;
	int* rep_cnt;
	queue<int>* produceQ;
	priority_queue<BFinfo, vector<BFinfo>, greater<BFinfo>>* resultQ;
};

void* BF(void* argu) {
	BruteForceClusterArgument* arg = (BruteForceClusterArgument*)argu;

	vector<int>& todo = *(arg->todo);
	int* rep_list = arg->rep_list;
	queue<int>& produceQ = *(arg->produceQ);
	priority_queue<BFinfo, vector<BFinfo>, greater<BFinfo>>& resultQ = *(arg->resultQ);

	while (1) {
		pthread_mutex_lock(arg->mutex);
		if (produceQ.empty()) {
			pthread_mutex_unlock(arg->mutex);
			continue;
		}
		int i = produceQ.front();
		if (i == INF) {
			pthread_mutex_unlock(arg->mutex);
			return NULL;
		}
		produceQ.pop();
		int mx = (*(arg->rep_cnt)>>3);
		//只搜索一半
		pthread_mutex_unlock(arg->mutex);

		int compress_min = INF;
		int ref_index = -1;
		for (int j = 0; j < mx; ++j) {
			int now = do_xdelta3(todo[i], rep_list[j], arg->id);
			
			if (now < compress_min) {
				compress_min = now;
				ref_index = j;
			}
		}
		pthread_mutex_lock(arg->mutex);
		resultQ.push({i, mx, compress_min, ref_index});
		pthread_mutex_unlock(arg->mutex);
	}
}

// todo: list of blocks not included in cluster
// cluster: first element is representative
void bruteForceCluster(vector<int>& todo, vector<vector<int>>& cluster, int threshold) {
	int MAX_QSIZE = 5 * NUM_THREAD;

	pthread_mutex_t mutex;
	pthread_mutex_init(&mutex, NULL);

	int* rep_list = new int[cluster.size() + todo.size()];
	int rep_cnt = 0;
	for (int i = 0; i < (int)cluster.size(); ++i) {
		rep_list[rep_cnt++] = cluster[i][0];
	}

	// Priority Queue: {index in todo, checked index of rep_list, size, ref index of rep_list}
	queue<int> produceQ;
	priority_queue<BFinfo, vector<BFinfo>, greater<BFinfo>> resultQ;
	
	for (int i = 0; i < min(MAX_QSIZE, todo.size()); ++i) produceQ.push(i);

	BruteForceClusterArgument arg[NUM_THREAD - 1];
	for (int i = 0; i < NUM_THREAD - 1; ++i) {
		arg[i].mutex = &mutex;
		arg[i].id = i;
		arg[i].todo = &todo;
		arg[i].rep_list = rep_list;
		arg[i].rep_cnt = &rep_cnt;
		arg[i].produceQ = &produceQ;
		arg[i].resultQ = &resultQ;
	}

	// Create thread
	pthread_t tid[NUM_THREAD];
	for (int i = 0; i < NUM_THREAD - 1; ++i) pthread_create(&tid[i], NULL, BF, (void*)&arg[i]);

	for (int i = 0; i < (int)todo.size(); ++i) {
		BFinfo now;
		while (1) {
			pthread_mutex_lock(&mutex);

			if (resultQ.empty()) now = {-1, -1, -1, -1};
			else now = resultQ.top();

			if (get<0>(now) == i) {
				resultQ.pop();
				pthread_mutex_unlock(&mutex);
				break;
			}
			pthread_mutex_unlock(&mutex);
		}

		int compress_min = get<2>(now);
		int ref_index = get<3>(now);

		//for (int j = get<1>(now); j < rep_cnt; ++j) {
		// for (int j = 0; j < (rep_cnt>>1); ++j) {
		// 	int e = do_xdelta3(todo[i], rep_list[j], NUM_THREAD - 1);
		// 	if (e < compress_min) {
		// 		compress_min = e;
		// 		ref_index = j;
		// 	}
		// }

		if (compress_min <= threshold) {
			cluster[ref_index].push_back(todo[i]);
		}
		else {
			cluster.push_back(vector<int>(1, todo[i]));
			pthread_mutex_lock(&mutex);
			rep_list[rep_cnt++] = todo[i];
			pthread_mutex_unlock(&mutex);
		}

		if (i + MAX_QSIZE < (int)todo.size()) {
			pthread_mutex_lock(&mutex);
			produceQ.push(i + MAX_QSIZE);
			pthread_mutex_unlock(&mutex);
		}

		// if (i % 10 == 9) {
		// 	fprintf(stderr, "%d, qsize: %d, rep_cnt: %d\n", i, (int)resultQ.size(), rep_cnt);
		// }
		fprintf(stderr, "%d, qsize: %d, rep_cnt: %d\n", i, (int)resultQ.size(), rep_cnt);
	}
	pthread_mutex_lock(&mutex);
	produceQ.push(INF);
	pthread_mutex_unlock(&mutex);

	for (int i = 0; i < NUM_THREAD - 1; ++i) pthread_join(tid[i], NULL);

	delete[] rep_list;

	return;
}

void print_cluster(vector<vector<int>>& cluster,char* filename) {
	char * tail = "_coarse.log";
	int tailsz = strlen(tail);
	int fileNamesz = strlen(filename);

	char * newfile = (char*)malloc((tailsz + fileNamesz +1) * sizeof(char));
	strcpy(newfile,filename);
	strcat(newfile,tail);
	printf("inputfile : %s\n",filename);
	printf("output log in: %s\n",newfile);
	FILE * outFile = fopen(newfile,"w");
	if(outFile == NULL){
		printf("write into file wrong!!\n");
	}
	for (int i = 0; i < cluster.size(); ++i) {
		//printf("cluster [%d] size: %d  ", i , cluster[i].size());
		fprintf(outFile,"%d", cluster[i].size());
		for (int u: cluster[i]) {
			//printf(" %d", u);
			fprintf(outFile," %d", u);
		}
		fprintf(outFile,"\n");
		//printf("\n");
	}
	fclose(outFile);
	free(newfile);
}

void read_file(char* name) {
	N = 0;
	trace.clear();

	FILE* f = fopen(name, "rb");
	while (1) {
		char* ptr = new char[BLOCK_SIZE];
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
}

int main(int argc, char* argv[]) {
	if (argc != 3) {
		cerr << "usage: ./coarse [input_file] [num_thread]\n";
		exit(0);
	}
	NUM_THREAD = atoi(argv[2]);

	read_file(argv[1]);

	set<uint64_t> dedup;
	vector<int> unique_list;
	for (int i = 0; i < N; ++i) {
		XXH64_hash_t h = XXH64(trace[i], BLOCK_SIZE, 0);

		if (dedup.count(h)) continue;
		else {
			dedup.insert(h);
			unique_list.push_back(i);
		}
	}

	vector<vector<int>> cluster;
	bruteForceCluster(unique_list, cluster, COARSE_T);

	unique_list.clear();
	vector<vector<int>> newcluster;
	for (int i = 0; i < cluster.size(); ++i) {
		int lz4_min = INF;
		int rep = -1;
		for (int j: cluster[i]) {
			double r = rand() / (double)RAND_MAX;
			if (r < ((int)cluster[i].size() - 1000) / (double)cluster[i].size()) continue;

			int sum = 0;
			for (int k: cluster[i]) {
				sum += do_xdelta3(j, k, 0);
			}

			if (sum < lz4_min) {
				lz4_min = sum;
				rep = j;
			}
		}
		newcluster.push_back(vector<int>(1, rep));
		for (int j: cluster[i]) {
			if (j != rep) unique_list.push_back(j);
		}
	}

	bruteForceCluster(unique_list, newcluster, COARSE_T);
	print_cluster(newcluster,argv[1]);
}
