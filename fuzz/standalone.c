#define _POSIX_C_SOURCE 200809L

/*
 * Standalone fuzz driver for compilers without libFuzzer (e.g. gcc).
 * Replays every file in the given directories, then runs mutated
 * variants of them. Corpus order is bytewise sorted so mutation streams match
 * across filesystems. usage: fuzz_X [-iterations N] [-save-input path] dir...
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

/* Preserve the current input before entering the compiler, including on hangs. */
static void
test_input(const uint8_t *data, size_t size, const char *save_path)
{
	if (save_path != NULL) {
		FILE *file = fopen(save_path, "wb");
		if (file == NULL) {
			fprintf(stderr, "cannot save fuzz input: %s\n", save_path);
			exit(1);
		}
		size_t written = fwrite(data, 1, size, file);
		int status = fclose(file);
		if (written != size || status != 0) {
			fprintf(stderr, "cannot save fuzz input: %s\n", save_path);
			exit(1);
		}
	}
	LLVMFuzzerTestOneInput(data, size);
}

static int
compare_entries(const struct dirent **lhs, const struct dirent **rhs)
{
	return strcmp((*lhs)->d_name, (*rhs)->d_name);
}

static void
load_file(const char *path, const char *save_path)
{
	if (corpus_count == 256)
		return;
	FILE *file = fopen(path, "rb");
	if (file == NULL)
		return;
	size_t got = fread(corpus[corpus_count], 1, MAX_INPUT, file);
	fclose(file);
	test_input(corpus[corpus_count], got, save_path);
	corpus_size[corpus_count] = got;
	corpus_count = corpus_count + 1;
}

static void
load_directory(const char *path, const char *save_path)
{
	struct dirent **entries;
	int count = scandir(path, &entries, NULL, compare_entries);
	if (count < 0) {
		load_file(path, save_path);
		return;
	}
	for (int i = 0; i < count; i++) {
		struct dirent *entry = entries[i];
		if (entry->d_name[0] != '.') {
			char full[4096];
			snprintf(full, sizeof(full), "%s/%s", path, entry->d_name);
			load_file(full, save_path);
		}
		free(entry);
	}
	free(entries);
}

static const char *const interesting[] = {
	"\"", "'", "\\\\", "//", "///", "//!", "\r", "\n", "\t", "@", "@\"", ".*", "..", "...",
	"0x", "1e", "1.", "<<|=", "+%=", "\x00", "\xff", "\xEF\xBB\xBF",
};

int
main(int argc, char **argv)
{
	uint32_t iterations = 20000;
	const char *save_path = NULL;
	int first = 1;
	while (first + 1 < argc) {
		if (strcmp(argv[first], "-iterations") == 0)
			iterations = (uint32_t)strtoul(argv[first + 1], NULL, 10);
		else if (strcmp(argv[first], "-save-input") == 0)
			save_path = argv[first + 1];
		else
			break;
		first += 2;
	}
	for (int i = first; i < argc; i++)
		load_directory(argv[i], save_path);
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
		test_input(buffer, size, save_path);
	}
	printf("fuzz-standalone: %u corpus files, %u mutated inputs, no failures\n", corpus_count, iterations);
	return 0;
}
