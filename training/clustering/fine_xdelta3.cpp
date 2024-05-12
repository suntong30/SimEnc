#include <iostream>
#include <vector>
#include <cmath>
#include <set>
#include <map>
#include <queue>
#include <thread>
#include "../lz4.h"
#include "../xxhash.h"
#include "../xdelta3/xdelta3.h"
// #define BLOCK_SIZE (512*1024)
#define BLOCK_SIZE (4*1024)
#define INF 987654321
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

typedef pair<int, int> ii;

struct RepArgument {
	pthread_mutex_t* mutex;
	int id;
	vector<vector<int>>* cluster;
	queue<ii>* todoQ;
	queue<ii>* doneQ;
};

void* RP(void* argu) {
	RepArgument* arg = (RepArgument*)argu;

	vector<vector<int>>& cluster = *(arg->cluster);
	queue<ii>& todoQ = *(arg->todoQ);
	queue<ii>& doneQ = *(arg->doneQ);

	while (1) {
		int i = -1, j = -1;

		pthread_mutex_lock(arg->mutex);
		if (!todoQ.empty()) {
			i = todoQ.front().first;
			j = todoQ.front().second;
			if (i == INF) {
				pthread_mutex_unlock(arg->mutex);
				break;
			}
			todoQ.pop();
		}
		pthread_mutex_unlock(arg->mutex);

		if (i != -1) {
			int sum = 0;
			for (int k: cluster[i])	{
				sum += do_xdelta3(cluster[i][j], k, arg->id);
			}
			pthread_mutex_lock(arg->mutex);
			doneQ.push({sum, j});
			pthread_mutex_unlock(arg->mutex);
		}
	}
	return NULL;
}

void chooseRep(vector<vector<int>>& cluster, vector<int>& todo) {
	todo.clear();
	vector<int> new_rep;

	int max_elem = -1;
	for (int i = 0; i < cluster.size(); ++i) max_elem = max(max_elem, cluster[i].size());

	if (max_elem < NUM_THREAD) {
		for (int i = 0; i < cluster.size(); ++i) {
			int sum_min = INF;
			int ref = -1;
			for (int u: cluster[i]) {
				int total = 0;
				for (int v: cluster[i]) {
					if (u == v) continue;
					int now = do_xdelta3(u, v, 0);
					total += now;
				}
				if (total < sum_min) {
					sum_min = total;
					ref = u;
				}
			}
			for (int u: cluster[i]) {
				if (u == ref) new_rep.push_back(u);
				else todo.push_back(u);
			}
		}
		cluster.clear();
		for (int u: new_rep) {
			cluster.push_back(vector<int>(1, u));
		}
		return;
	}

	pthread_mutex_t mutex;
	pthread_mutex_init(&mutex, NULL);

	queue<ii> todoQ;
	queue<ii> doneQ;

	RepArgument arg[NUM_THREAD - 1];
	for (int i = 0; i < NUM_THREAD - 1; ++i) {
		arg[i].mutex = &mutex;
		arg[i].id = i;
		arg[i].cluster = &cluster;
		arg[i].todoQ = &todoQ;
		arg[i].doneQ = &doneQ;
	}

	pthread_t tid[NUM_THREAD - 1];
	for (int i = 0; i < NUM_THREAD - 1; ++i) pthread_create(&tid[i], NULL, RP, (void*)&arg[i]);

	for (int i = 0; i < cluster.size(); ++i) {
		if (cluster[i].size() == 1) {
			new_rep.push_back(cluster[i][0]);
			continue;
		}

		int lz4_min = INF;
		int rep = -1;
		for (int j = 0; j < cluster[i].size(); ++j) {
			pthread_mutex_lock(&mutex);
			todoQ.push({i, j});
			pthread_mutex_unlock(&mutex);
		}
		int cnt = 0;
		while (cnt < cluster[i].size()) {
			pthread_mutex_lock(&mutex);
			if (!doneQ.empty()) {
				if (doneQ.front().first < lz4_min) {
					lz4_min = doneQ.front().first;
					rep = doneQ.front().second;
				}
				doneQ.pop();
				cnt++;
			}
			pthread_mutex_unlock(&mutex);
		}
		for (int j = 0; j < cluster[i].size(); ++j) {
			if (j != rep) todo.push_back(cluster[i][j]);
			else new_rep.push_back(cluster[i][j]);
		}
	}

	pthread_mutex_lock(&mutex);
	todoQ.push({INF, -1});
	pthread_mutex_unlock(&mutex);
	for (int i = 0; i < NUM_THREAD - 1; ++i) pthread_join(tid[i], NULL);

	cluster.clear();
	for (int u: new_rep) {
		cluster.push_back(vector<int>(1, u));
	}
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
		int mx = *(arg->rep_cnt);
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
	if (todo.size() < NUM_THREAD) {
		for (int u: todo) {
			int compress_min = INF;
			int ref_index = -1;
			for (int i = 0; i < cluster.size(); ++i) {
				int now = do_xdelta3(u, cluster[i][0], 0);
				if (now < compress_min) {
					compress_min = now;
					ref_index = i;
				}
			}
			if (compress_min <= threshold) {
				cluster[ref_index].push_back(u);
			}
			else {
				cluster.push_back(vector<int>(1, u));
			}
		}
		return;
	}

	int MAX_QSIZE = 2 * NUM_THREAD;

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

		for (int j = get<1>(now); j < rep_cnt; ++j) {
			int e = do_xdelta3(todo[i], rep_list[j], NUM_THREAD - 1);
			if (e < compress_min) {
				compress_min = e;
				ref_index = j;
			}
		}

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

		if (i % 10 == 9) {
			fprintf(stderr, "%d, qsize: %d, rep_cnt: %d\n", i, (int)resultQ.size(), rep_cnt);
		}
	}
	pthread_mutex_lock(&mutex);
	produceQ.push(INF);
	pthread_mutex_unlock(&mutex);

	for (int i = 0; i < NUM_THREAD - 1; ++i) pthread_join(tid[i], NULL);

	delete[] rep_list;

	return;
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

void print_cluster(vector<vector<int>>& cluster,char* filename) {
	char * tail = "_fine.log";
	int tailsz = strlen(tail);
	int fileNamesz = strlen(filename);

	char * newfile = (char*)malloc((tailsz + fileNamesz +1) * sizeof(char));
	strcpy(newfile,filename);
	strcat(newfile,tail);
	// printf("inputfile : %s\n",filename);
	// printf("output log in: %s\n",newfile);
	FILE * outFile = fopen(newfile,"a");
	if(outFile == NULL){
		printf("write into file wrong!!\n");
	}
	for (int i = 0; i < cluster.size(); ++i) {

		fprintf(outFile,"%d", cluster[i].size());
		for (int u: cluster[i]) {
	
			fprintf(outFile," %d", u);
		}
		fprintf(outFile,"\n");

	}
	fclose(outFile);
	free(newfile);
}

int main(int argc, char* argv[]) {
	if (argc != 4) {
		cerr << "usage: ./fine [input_file] [num_thread] [coarse_log]\n";
		exit(0);
	}
		
	NUM_THREAD = atoi(argv[2]);
	FILE* logfile = fopen(argv[3], "r");
	read_file(argv[1]);

	int sz, t;
	while (fscanf(logfile,"%d", &sz) == 1) {
		vector<int> unique_list;
		for (int i = 0; i < sz; ++i) {
			fscanf(logfile ,"%d", &t);
			unique_list.push_back(t);
		}
		if (sz == 1) {
			printf("%d %d \n", sz, t);
			continue;
		}

		fprintf(stderr, "Cluster of size %d: %d\n", sz, unique_list[0]);

		vector<vector<int>> ans;
		int optimal = INF;
		int op_th;

		// for (int threshold = 512*1024/64; threshold <= 512*1024/8; threshold += 512*1024/64) {
		for (int threshold = 4*1024/64; threshold <= 4*1024/8; threshold += 4*1024/64) {
		// for (int threshold = 64; threshold <= 512; threshold += 64) {
			vector<vector<int>> cluster;
			set<int> rep_pre;
			bruteForceCluster(unique_list, cluster, threshold);

			for (int epoch = 0; epoch < 2; ++epoch) {
				vector<int> todo;
				chooseRep(cluster, todo);
				bruteForceCluster(todo, cluster, threshold);
			}

			int lz4_sum = 0;
			int total = 0;
			for (int i = 0; i < cluster.size(); ++i) {
				lz4_sum += LZ4_compress_default(trace[cluster[i][0]], compressed[0], BLOCK_SIZE, 2 * BLOCK_SIZE);
				total++;
				for (int j = 1; j < cluster[i].size(); ++j) {
					lz4_sum += do_xdelta3(cluster[i][0], cluster[i][j], 0);
					total++;
				}
			}

			fprintf(stderr, "Threshold %d: Total cnt %d, Total lz4 %d\n", threshold, total, lz4_sum);

			if (lz4_sum < optimal) {
				optimal = lz4_sum;
				op_th = threshold;
				ans = cluster;
			}
			else if (lz4_sum > optimal) break;
		}

		fprintf(stderr, "Best: %d (Threshold: %d)\n", optimal, op_th);
		print_cluster(ans,argv[1]);






		// for (int i = 0; i < ans.size(); ++i) {
		// 	printf("%d ", ans[i].size());
		// 	for (int j = 0; j < ans[i].size(); ++j) {
		// 		printf("%d ", ans[i][j]);
		// 	}
		// 	printf("\n");
		// }
	}
}
