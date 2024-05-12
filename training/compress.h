#include <vector>
#include <bitset>
#include <iostream>
#include <sys/time.h>
#include <unistd.h>

// #define BLOCK_SIZE 4096
// #define BLOCK_SIZE (512*1024)
#define BLOCK_SIZE (4*1024)

// offset: 40bit (~1TB)
// size: 12bit (~4KB)
// ref: 28bit (1TB / 4KB)
// flag: 2bit (00: not-compressed, 01: self-compressed, 10: deduped, 11: delta-compressed)
// total: 40 + 12 + 28 + 2 = 82bit

typedef std::bitset<82> RECIPE;

static inline void set_offset(RECIPE& r, unsigned long long t) { r |= (RECIPE(t) << 42); }
static inline void set_size(RECIPE& r, unsigned long t) { r |= (RECIPE(t) << 30); }
static inline void set_ref(RECIPE& r, unsigned long t) { r |= (RECIPE(t) << 2); }
static inline void set_flag(RECIPE& r, unsigned long t) { r |= (RECIPE(t) << 0); }
static inline unsigned long long get_offset(RECIPE& r) { return ((r << 0) >> 42).to_ullong(); }
static inline unsigned long get_size(RECIPE& r) { return ((r << 40) >> 70).to_ulong(); }
static inline unsigned long get_ref(RECIPE& r) { return ((r << 52) >> 54).to_ulong(); }
static inline unsigned long get_flag(RECIPE& r) { return ((r << 80) >> 80).to_ulong(); }

char compressed[2 * BLOCK_SIZE];
char delta_compressed[2 * BLOCK_SIZE];

struct DATA_IO {
	int N;
	std::vector<char*> trace;
	std::vector<RECIPE> recipe;

	char fileName[500];
	char outputFileName[500];
	char recipeName[500];

	FILE* out;

	struct timeval start_time, end_time;

	DATA_IO(char* name);
	~DATA_IO();
	void read_file();
	inline void write_file(char* data, int size);
	inline void recipe_insert(RECIPE r);
	void recipe_write();
	void time_check_start();
	long long time_check_end();
};

DATA_IO::DATA_IO(char* name) {
	sprintf(fileName, "%s", name);
	sprintf(outputFileName, "%s_output", name);
	sprintf(recipeName, "%s_recipe", name);

	out = NULL;
}

DATA_IO::~DATA_IO() {
	if (out) fclose(out);

	for (int i = 0; i < N; ++i) {
		free(trace[i]);
	}
}

void DATA_IO::read_file() {
	N = 0;
	trace.clear();

	FILE* f = fopen(fileName, "rb");
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

inline void DATA_IO::write_file(char* data, int size) {
	if (out == NULL) out = fopen(outputFileName, "wb");
	fwrite(data, size, 1, out);
}

inline void DATA_IO::recipe_insert(RECIPE r) {
	recipe.push_back(r);
}

void DATA_IO::recipe_write() {
	FILE* f = fopen(recipeName, "wb");
	for (int i = 0; i < N; ++i) {
		fwrite(&recipe[i], 1, sizeof(RECIPE), f);
	}
	fclose(f);
}

void DATA_IO::time_check_start() {
	gettimeofday(&start_time, NULL);
}

long long DATA_IO::time_check_end() {
	gettimeofday(&end_time, NULL);
	return (end_time.tv_sec - start_time.tv_sec) * 1000000 + (end_time.tv_usec - start_time.tv_usec);
}
