/*
 * Standalone fuzz driver for compilers without libFuzzer (e.g. gcc).
 * Replays every file in the given directories, then runs mutated
 * variants of them. usage: fuzz_X [-iterations N] dir...
 */
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

#define MAX_INPUT 65536

static uint8_t corpus[256][MAX_INPUT];
static size_t corpus_size[256];
static uint32_t corpus_count = 0;

static uint64_t rng_state = 0x9E3779B97F4A7C15u;

static uint64_t
next_random(void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 7;
	rng_state ^= rng_state << 17;
	return rng_state;
}

static void
load_file(const char *path)
{
	if (corpus_count == 256)
		return;
	FILE *file = fopen(path, "rb");
	if (file == NULL)
		return;
	size_t got = fread(corpus[corpus_count], 1, MAX_INPUT, file);
	fclose(file);
	LLVMFuzzerTestOneInput(corpus[corpus_count], got);
	corpus_size[corpus_count] = got;
	corpus_count = corpus_count + 1;
}

static void
load_directory(const char *path)
{
	DIR *dir = opendir(path);
	if (dir == NULL) {
		load_file(path);
		return;
	}
	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_name[0] == '.')
			continue;
		char full[4096];
		snprintf(full, sizeof(full), "%s/%s", path, entry->d_name);
		load_file(full);
	}
	closedir(dir);
}

static const char *const interesting[] = {
	"\"", "'", "\\\\", "//", "///", "//!", "\r", "\n", "\t", "@", "@\"", ".*", "..", "...",
	"0x", "1e", "1.", "<<|=", "+%=", "\x00", "\xff", "\xEF\xBB\xBF",
};

int
main(int argc, char **argv)
{
	uint32_t iterations = 20000;
	int first = 1;
	if (argc >= 3 && strcmp(argv[1], "-iterations") == 0) {
		iterations = (uint32_t)strtoul(argv[2], NULL, 10);
		first = 3;
	}
	for (int i = first; i < argc; i++)
		load_directory(argv[i]);
	if (corpus_count == 0) {
		corpus_size[0] = 0;
		corpus_count = 1;
	}

	static uint8_t buffer[MAX_INPUT];
	for (uint32_t i = 0; i < iterations; i++) {
		uint32_t pick = (uint32_t)(next_random() % corpus_count);
		size_t size = corpus_size[pick];
		memcpy(buffer, corpus[pick], size);
		uint32_t mutations = 1 + (uint32_t)(next_random() % 8);
		for (uint32_t m = 0; m < mutations; m++) {
			uint64_t choice = next_random() % 3;
			if (choice == 0 && size > 0) {
				buffer[next_random() % size] = (uint8_t)next_random();
			} else if (choice == 1 && size > 0) {
				size_t at = next_random() % size;
				memmove(buffer + at, buffer + at + 1, size - at - 1);
				size = size - 1;
			} else {
				const char *piece = interesting[next_random() % (sizeof(interesting) / sizeof(interesting[0]))];
				size_t length = strlen(piece);
				if (length == 0)
					length = 1;
				if (size + length >= MAX_INPUT)
					continue;
				size_t at = size == 0 ? 0 : next_random() % size;
				memmove(buffer + at + length, buffer + at, size - at);
				memcpy(buffer + at, piece, length);
				size = size + length;
			}
		}
		LLVMFuzzerTestOneInput(buffer, size);
	}
	printf("fuzz-standalone: %u corpus files, %u mutated inputs, no failures\n", corpus_count, iterations);
	return 0;
}
