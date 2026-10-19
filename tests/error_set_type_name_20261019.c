/* Internal diagnostic-renderer policy only; not Zig name legality or @typeName.
 * Sema is opaque in sema.h. Include its implementation once to construct a
 * white-box finite Type fixture without parser programs, Values or API hooks.
 * The shell runner must NOT also link a separate sema.c translation unit. */
#include "sema.c"

static void
print_hex(const char *text)
{
	for (size_t i = 0; text[i] != '\0'; i++)
		fprintf(stderr, "%02x", (unsigned char)text[i]);
}

int
main(void)
{
	static const struct {
		const char *label;
		const char *member; /* NULL denotes the empty SET, not an empty member. */
		uint32_t length;
		const char *expected;
	} vectors[] = {
		{"empty-set", NULL, 0, "error{}"},
		{"plain", "Plain", 5, "error{@\"Plain\"}"},
		{"quote", "\"", 1, "error{@\"\\\"\"}"},
		{"backslash", "\\", 1, "error{@\"\\\\\"}"},
		{"control", "\001", 1, "error{@\"\\x01\"}"},
		{"DEL", "\177", 1, "error{@\"\\x7f\"}"},
		{"raw-ff", "\377", 1, "error{@\"\\xff\"}"},
		{"UTF8-bytes", "\303\251", 2, "error{@\"\\xc3\\xa9\"}"},
	};
	unsigned failures = 0;
	for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
		Arena arena;
		arena_init(&arena);
		Interner interner;
		intern_init(&interner, &arena);
		StringId name = vectors[i].member == NULL ? STRING_NONE
			: intern(&interner, vectors[i].member, vectors[i].length);
		Type type = {.kind = TYPE_ERROR_SET,
			.as.error_set = {.names = name == STRING_NONE ? NULL : &name,
				.count = name == STRING_NONE ? 0 : 1}};
		Sema sema = {.arena = &arena, .interner = &interner,
			.types = &type, .type_count = 1, .type_capacity = 1};
		const char *actual = sema_type_name(&sema, 0);
		size_t expected_length = strlen(vectors[i].expected);
		/* Compare full bytes and the terminator, after checking actual length. */
		bool ok = strlen(actual) == expected_length
			&& memcmp(actual, vectors[i].expected, expected_length + 1) == 0;
		for (size_t j = 0; actual[j] != '\0'; j++)
			if ((unsigned char)actual[j] < 32 || (unsigned char)actual[j] >= 127)
				ok = false;
		if (!ok) {
			fprintf(stderr, "FAIL %s expected_hex=", vectors[i].label);
			print_hex(vectors[i].expected);
			fputs(" actual_hex=", stderr);
			print_hex(actual);
			fputc('\n', stderr);
			failures++;
		} else {
			printf("PASS %s\n", vectors[i].label);
		}
		arena_free(&arena);
	}
	printf("internal finite-error-set renderer: 8 vectors, %u failures\n", failures);
	return failures == 0 ? 0 : 1;
}
