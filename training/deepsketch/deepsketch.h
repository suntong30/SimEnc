#include <torch/script.h>
#include <tuple>
#include <map>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <functional>
#include <random>
#include <iostream>
#include "../xxhash.h"
#include "NGT/Index.h"

// Network

#define HASH_SIZE 128
typedef std::bitset<HASH_SIZE> MYHASH;

class NetworkHash {
	private:
	int BATCH_SIZE;
	torch::jit::script::Module module;
	float* data;
	bool* memout;
	int* index;
	int cnt;

	public:
	NetworkHash(int BATCH_SIZE, char* module_name) {
		this->BATCH_SIZE = BATCH_SIZE;
		this->module = torch::jit::load(module_name);
		this->module.to(at::kCUDA);
		this->module.eval();
		this->data = new float[BATCH_SIZE * BLOCK_SIZE];
		this->memout = new bool[BATCH_SIZE * HASH_SIZE];
		this->index = new int[BATCH_SIZE];
		this->cnt = 0;
	}
	~NetworkHash() {
		delete[] this->data;
		delete[] this->memout;
		delete[] this->index;
	}
	bool push(char* ptr, int label);
	std::vector<std::pair<MYHASH, int>> request();
};

bool NetworkHash::push(char* ptr, int label) {
	for (int i = 0; i < BLOCK_SIZE; ++i) {
		data[cnt * BLOCK_SIZE + i] = ((int)(unsigned char)(ptr[i]) - 128) / 128.0;
	}
	index[cnt++] = label;

	if (cnt == BATCH_SIZE) return true;
	else return false;
}

std::vector<std::pair<MYHASH, int>> NetworkHash::request() {
	if (cnt == 0) return std::vector<std::pair<MYHASH, int>>();

	std::vector<std::pair<MYHASH, int>> ret(cnt);

	std::vector<torch::jit::IValue> inputs;
	torch::Tensor t = torch::from_blob(data, {cnt, BLOCK_SIZE}).to(torch::kCUDA);
	inputs.push_back(t);

	torch::Tensor output = module.forward(inputs).toTensor().cpu();

	torch::Tensor comp = output.ge(0.0);
	memcpy(memout, comp.cpu().data_ptr<bool>(), cnt * HASH_SIZE);

	bool* ptr = this->memout;

	for (int i = 0; i < cnt; ++i) {
		for (int j = 0; j < HASH_SIZE; ++j) {
			if (ptr[HASH_SIZE * i + j]) ret[i].first.flip(j);
		}
		ret[i].second = index[i];
	}

	cnt = 0;
	return ret;
}

// ANN

class ANN {
	private:
		int ANN_SEARCH_CNT, LINEAR_SIZE, NUM_THREAD, THRESHOLD;
		std::vector<MYHASH> linear;
		std::unordered_map<MYHASH, std::vector<int>> hashtable;
		NGT::Property* property;
		NGT::Index* index;

	public:
	ANN(int ANN_SEARCH_CNT, int LINEAR_SIZE, int NUM_THREAD, int THRESHOLD, NGT::Property* property, NGT::Index* index) {
		this->ANN_SEARCH_CNT = ANN_SEARCH_CNT; // The number of candidates extract from ANN class
		this->LINEAR_SIZE = LINEAR_SIZE; // Size of linear buffer
		this->NUM_THREAD = NUM_THREAD;
		this->THRESHOLD = THRESHOLD;
		this->property = property;
		this->index = index;
	}
	int request(MYHASH h);
	void insert(MYHASH h, int label);
};

int ANN::request(MYHASH h) {
	int dist = 999;
	int ret = -1;

	// scan list
	for (int i = linear.size() - 1; i >= 0; --i) {
		int nowdist = (linear[i] ^ h).count();
		if (dist > nowdist) {
			dist = nowdist;
			ret = hashtable[linear[i]].back();
		}
	}

	std::vector<uint8_t> query;
	for (int i = 0; i < property->dimension; ++i) {
		query.push_back((uint8_t)((h << (HASH_SIZE - 8 * i - 8)) >> (HASH_SIZE - 8)).to_ulong());
	}

	NGT::SearchQuery sc(query);
	NGT::ObjectDistances objects;
	sc.setResults(&objects);
	sc.setSize(this->ANN_SEARCH_CNT);
	sc.setEpsilon(0.2);

	index->search(sc);
	for (int i = 0; i < objects.size(); ++i) {
		int nowdist = objects[i].distance;

		if (dist > nowdist) {
			MYHASH now;

			NGT::ObjectSpace& objectSpace = index->getObjectSpace();
			uint8_t *object = static_cast<uint8_t*>(objectSpace.getObject(objects[i].id));
			for (int j = 0; j < objectSpace.getDimension(); ++j) {
				for (int k = 0; k < 8; ++k) {
					if (object[j] & (1 << k)) {
						now.flip(8 * j + k);
					}
				}
			}
			dist = nowdist;
			ret = hashtable[now].back();
		}
		else if (dist == nowdist) {
			MYHASH now;

			NGT::ObjectSpace& objectSpace = index->getObjectSpace();
			uint8_t *object = static_cast<uint8_t*>(objectSpace.getObject(objects[i].id));
			for (int j = 0; j < objectSpace.getDimension(); ++j) {
				for (int k = 0; k < 8; ++k) {
					if (object[j] & (1 << k)) {
						now.flip(8 * j + k);
					}
				}
			}
			int nowindex = hashtable[now].back();

			if (nowindex > ret) ret = nowindex;
		}
	}

	if (dist <= THRESHOLD) return ret;
	else return -1;
}

void ANN::insert(MYHASH h, int label) {
	if (hashtable.count(h)) {
		hashtable[h].push_back(label);
		return;
	}

	hashtable[h].push_back(label);
	linear.push_back(h);

	if (linear.size() == LINEAR_SIZE) {
		for (int i = 0; i < linear.size(); ++i) {
			std::vector<uint8_t> query;
			for (int j = 0; j < property->dimension; ++j) {
				query.push_back((uint8_t)((linear[i] << (HASH_SIZE - 8 * j - 8)) >> (HASH_SIZE - 8)).to_ulong());
			}
			index->append(query);
		}
		index->createIndex(NUM_THREAD);

		linear.clear();
	} 
}
