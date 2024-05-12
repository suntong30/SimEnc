#include <tuple>
#include <limits>
#include <map>
#include <vector>
#include <algorithm>
#include <functional>
#include <random>
#include "../xxhash.h"

// finesse

class Finesse {
	private:
	std::mt19937 gen1, gen2;
	std::uniform_int_distribution<uint32_t> full_uint32_t;

	int BLOCK_SIZE, W;
	int SF_NUM, FEATURE_NUM;

	uint32_t* TRANSPOSE_M;
	uint32_t* TRANSPOSE_A;
	int* subchunkIndex;

	const uint32_t A = 37, MOD = 1000000007;
	uint64_t Apower = 1;

	uint32_t* feature;
	uint64_t* superfeature;

	std::map<uint64_t, std::vector<int>>* sfTable;
	public:
	Finesse(int _BLOCK_SIZE, int _W, int _SF_NUM, int _FEATURE_NUM) {
		gen1 = std::mt19937(922);
		gen2 = std::mt19937(314159);
		full_uint32_t = std::uniform_int_distribution<uint32_t>(std::numeric_limits<uint32_t>::min(), std::numeric_limits<uint32_t>::max());

		BLOCK_SIZE = _BLOCK_SIZE;
		W = _W;
		SF_NUM = _SF_NUM;
		FEATURE_NUM = _FEATURE_NUM;

		TRANSPOSE_M = new uint32_t[FEATURE_NUM];
		TRANSPOSE_A = new uint32_t[FEATURE_NUM];

		feature = new uint32_t[FEATURE_NUM];
		superfeature = new uint64_t[SF_NUM];
		subchunkIndex = new int[FEATURE_NUM + 1];
		subchunkIndex[0] = 0;
		for (int i = 0; i < FEATURE_NUM; ++i) {
			subchunkIndex[i + 1] = (BLOCK_SIZE * (i + 1)) / FEATURE_NUM;
		}

		sfTable = new std::map<uint64_t, std::vector<int>>[SF_NUM];

		for (int i = 0; i < FEATURE_NUM; ++i) {
			TRANSPOSE_M[i] = ((full_uint32_t(gen1) >> 1) << 1) + 1;
			TRANSPOSE_A[i] = full_uint32_t(gen1);
		}
		for (int i = 0; i < W - 1; ++i) {
			Apower *= A;
			Apower %= MOD;
		}
	}
	~Finesse() {
		delete[] TRANSPOSE_M;
		delete[] TRANSPOSE_A;
		delete[] feature;
		delete[] superfeature;
		delete[] subchunkIndex;
		delete[] sfTable;
	}
	int request(unsigned char* ptr);
	void insert(int label);
};

int Finesse::request(unsigned char* ptr) {
	for (int i = 0; i < FEATURE_NUM; ++i) feature[i] = 0;
	for (int i = 0; i < SF_NUM; ++i) superfeature[i] = 0;

	for (int i = 0; i < FEATURE_NUM; ++i) {
		int64_t fp = 0;

		for (int j = subchunkIndex[i]; j < subchunkIndex[i] + W; ++j) {
			fp *= A;
			fp += (unsigned char)ptr[j];
			fp %= MOD;
		}

		for (int j = subchunkIndex[i]; j < subchunkIndex[i + 1] - W + 1; ++j) {
			if (fp > feature[i]) feature[i] = fp;

			fp -= (ptr[j] * Apower) % MOD;
			while (fp < 0) fp += MOD;
			if (j != subchunkIndex[i + 1] - W) {
				fp *= A;
				fp += ptr[j + W];
				fp %= MOD;
			}
		}
	}

	for (int i = 0; i < FEATURE_NUM / SF_NUM; ++i) {
		std::sort(feature + i * SF_NUM, feature + (i + 1) * SF_NUM);
	}

	for (int i = 0; i < SF_NUM; ++i) {
		uint64_t temp[FEATURE_NUM / SF_NUM];
		for (int j = 0; j < FEATURE_NUM / SF_NUM; ++j) {
			temp[j] = feature[j * SF_NUM + i];
		}
		superfeature[i] = XXH64(temp, sizeof(uint64_t) * FEATURE_NUM / SF_NUM, 0);
	}

	uint32_t r = full_uint32_t(gen2) % SF_NUM;
	for (int i = 0; i < SF_NUM; ++i) {
		int index = (r + i) % SF_NUM;
		if (sfTable[index].count(superfeature[index])) {
			return sfTable[index][superfeature[index]].back();
		}
	}
	return -1;
}

// insert "prev calculated" sf: label
void Finesse::insert(int label) {
	for (int i = 0; i < SF_NUM; ++i) {
		sfTable[i][superfeature[i]].push_back(label);
	}
}
