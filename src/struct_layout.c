/* Exact specialization of Zig 0.17.0 std.sort.pdqContext and its insertion/
 * heap helpers, for declaration indices ordered by descending alignment.
 * Sources: lib/std/sort/pdq.zig and lib/std/sort.zig in the pinned Zig tree.
 * Copyright (c) Zig contributors. MIT (Expat); see third_party/LICENSE-ZIG.
 * Keep swaps and equal-key decisions unchanged: they determine field offsets.
 */
#include "struct_layout.h"

#include <assert.h>
#include <stdbool.h>

typedef struct SortContext {
	uint32_t *order;
	const uint32_t *alignments;
} SortContext;

typedef enum Hint {
	HINT_INCREASING,
	HINT_DECREASING,
	HINT_UNKNOWN,
} Hint;

typedef struct Range {
	uint32_t a;
	uint32_t b;
	uint32_t limit;
	bool leftmost;
	bool balanced;
	bool partitioned;
} Range;

static bool
less_than(SortContext context, uint32_t a, uint32_t b)
{
	return context.alignments[context.order[a]] > context.alignments[context.order[b]];
}

static void
swap(SortContext context, uint32_t a, uint32_t b)
{
	uint32_t tmp = context.order[a];
	context.order[a] = context.order[b];
	context.order[b] = tmp;
}

static void
insertion_sort(uint32_t a, uint32_t b, SortContext context)
{
	/* Unlike Zig's usize bounds, the C bounds can reach UINT32_MAX. */
	if (b - a < 2)
		return;
	for (uint32_t i = a + 1; i < b; i++) {
		uint32_t j = i;
		while (j > a && less_than(context, j, j - 1)) {
			swap(context, j, j - 1);
			j--;
		}
	}
}

static void
unguarded_insertion_sort(uint32_t a, uint32_t b, SortContext context)
{
	if (b - a < 2)
		return;
	for (uint32_t i = a + 1; i < b; i++) {
		uint32_t j = i;
		while (less_than(context, j, j - 1)) {
			swap(context, j, j - 1);
			j--;
		}
	}
}

static void
sift_down(uint32_t a, uint32_t target, uint32_t b, SortContext context)
{
	uint32_t cur = target;
	for (;;) {
		/* The pinned x86_64 usize calculation does not overflow for u32 bounds. */
		uint64_t wide_child = (uint64_t)(cur - a) * 2 + a + 1;
		if (wide_child >= b)
			break;
		uint32_t child = (uint32_t)wide_child;
		uint32_t next_child = child + 1;
		if (next_child < b && less_than(context, child, next_child))
			child = next_child;
		if (less_than(context, child, cur))
			break;
		swap(context, child, cur);
		cur = child;
	}
}

static void
heap_sort(uint32_t a, uint32_t b, SortContext context)
{
	uint32_t i = a + (b - a) / 2;
	while (i > a) {
		i--;
		sift_down(a, i, b, context);
	}
	i = b;
	while (i > a) {
		i--;
		swap(context, a, i);
		sift_down(a, a, i, context);
	}
}

static bool
partition(uint32_t a, uint32_t b, uint32_t *pivot, SortContext context)
{
	swap(context, a, *pivot);
	uint32_t i = a + 1;
	uint32_t j = b - 1;
	while (i <= j && less_than(context, i, a))
		i++;
	while (i <= j && !less_than(context, j, a))
		j--;
	if (i > j) {
		swap(context, j, a);
		*pivot = j;
		return true;
	}
	swap(context, i, j);
	i++;
	j--;

	/* Zig's cache-line annotation is a performance hint, not a swap decision. */
	uint8_t offsets_l[64];
	uint8_t offsets_r[64];
	uint32_t offsets_l_base = i;
	uint32_t offsets_r_base = j;
	uint32_t num_l = 0;
	uint32_t num_r = 0;
	uint32_t start_l = 0;
	uint32_t start_r = 0;
	while (i <= j) {
		uint32_t num_unknown = j + 1 - i;
		uint32_t left_split = 0;
		uint32_t right_split = 0;
		if (num_l == 0) {
			uint32_t available = num_r == 0 ? num_unknown / 2 : num_unknown;
			left_split = available < 64 ? available : 64;
		}
		if (num_r == 0) {
			uint32_t available = num_unknown - left_split;
			right_split = available < 64 ? available : 64;
		}
		for (uint32_t k = 0; k < left_split; k++) {
			offsets_l[num_l] = (uint8_t)k;
			num_l += !less_than(context, i + k, a);
		}
		i += left_split;
		for (uint32_t k = 0; k < right_split; k++) {
			offsets_r[num_r] = (uint8_t)k;
			num_r += less_than(context, j - k, a);
		}
		j -= right_split;
		uint32_t num = num_l < num_r ? num_l : num_r;
		for (uint32_t m = 0; m < num; m++)
			swap(context, offsets_l_base + offsets_l[start_l + m], offsets_r_base - offsets_r[start_r + m]);
		num_l -= num;
		num_r -= num;
		start_l += num;
		start_r += num;
		if (num_l == 0) {
			start_l = 0;
			offsets_l_base = i;
		}
		if (num_r == 0) {
			start_r = 0;
			offsets_r_base = j;
		}
	}
	if (num_l > 0) {
		while (num_l > 0) {
			num_l--;
			swap(context, offsets_l_base + offsets_l[start_l + num_l], j);
			j--;
		}
		i = j + 1;
	}
	if (num_r > 0) {
		while (num_r > 0) {
			num_r--;
			swap(context, offsets_r_base - offsets_r[start_r + num_r], i);
			i++;
		}
		j = i - 1;
	}
	swap(context, j, a);
	*pivot = j;
	return false;
}

static uint32_t
partition_equal(uint32_t a, uint32_t b, uint32_t pivot, SortContext context)
{
	swap(context, a, pivot);
	uint32_t i = a + 1;
	uint32_t j = b - 1;
	for (;;) {
		while (i <= j && !less_than(context, a, i))
			i++;
		while (i <= j && less_than(context, a, j))
			j--;
		if (i > j)
			break;
		swap(context, i, j);
		i++;
		j--;
	}
	return i;
}

static bool
partial_insertion_sort(uint32_t a, uint32_t b, SortContext context)
{
	uint32_t i = a + 1;
	for (uint32_t step = 0; step < 5; step++) {
		while (i < b && !less_than(context, i, i - 1))
			i++;
		if (i == b)
			return true;
		if (b - a < 50)
			return false;
		swap(context, i, i - 1);
		if (i - a >= 2) {
			uint32_t j = i - 1;
			while (j > a) {
				if (!less_than(context, j, j - 1))
					break;
				swap(context, j, j - 1);
				j--;
			}
		}
		if (b - i >= 2) {
			uint32_t j = i + 1;
			while (j < b) {
				if (!less_than(context, j, j - 1))
					break;
				swap(context, j, j - 1);
				j++;
			}
		}
	}
	return false;
}

static void
break_patterns(uint32_t a, uint32_t b, SortContext context)
{
	uint32_t len = b - a;
	if (len < 8)
		return;
	uint64_t random = len;
	uint64_t modulus = 1;
	while (modulus < len)
		modulus <<= 1;
	uint32_t center = a + (len / 4) * 2;
	for (uint32_t i = center - 1; i <= center + 1; i++) {
		random ^= random << 13;
		random ^= random >> 7;
		random ^= random << 17;
		uint32_t other = (uint32_t)(random & (modulus - 1));
		if (other >= len)
			other -= len;
		swap(context, i, a + other);
	}
}

static void
sort_three(uint32_t a, uint32_t b, uint32_t c, uint32_t *swaps, SortContext context)
{
	if (less_than(context, b, a)) {
		(*swaps)++;
		swap(context, b, a);
	}
	if (less_than(context, c, b)) {
		(*swaps)++;
		swap(context, c, b);
	}
	if (less_than(context, b, a)) {
		(*swaps)++;
		swap(context, b, a);
	}
}

static Hint
choose_pivot(uint32_t a, uint32_t b, uint32_t *pivot, SortContext context)
{
	uint32_t len = b - a;
	uint32_t i = a + len / 4;
	uint32_t j = a + len / 4 * 2;
	uint32_t k = a + len / 4 * 3;
	uint32_t swaps = 0;
	if (len >= 8) {
		if (len >= 50) {
			sort_three(i - 1, i, i + 1, &swaps, context);
			sort_three(j - 1, j, j + 1, &swaps, context);
			sort_three(k - 1, k, k + 1, &swaps, context);
		}
		sort_three(i, j, k, &swaps, context);
	}
	*pivot = j;
	return swaps == 0 ? HINT_INCREASING : swaps == 12 ? HINT_DECREASING : HINT_UNKNOWN;
}

static void
reverse_range(uint32_t a, uint32_t b, SortContext context)
{
	uint32_t i = a;
	uint32_t j = b - 1;
	while (i < j) {
		swap(context, i, j);
		i++;
		j--;
	}
}

static void
pdq_sort(SortContext context, uint32_t count, uint32_t limit)
{
	/* At most log2(count) pending ranges; count has 32 bits. */
	Range stack[32];
	Range range = {0, count, limit, true, true, true};
	uint32_t top = 0;
	for (;;) {
		for (;;) {
			uint32_t len = range.b - range.a;
			if (len <= 24) {
				if (range.leftmost)
					insertion_sort(range.a, range.b, context);
				else
					unguarded_insertion_sort(range.a, range.b, context);
				break;
			}
			if (range.limit == 0) {
				heap_sort(range.a, range.b, context);
				break;
			}
			if (!range.balanced) {
				break_patterns(range.a, range.b, context);
				range.limit--;
			}
			uint32_t pivot;
			Hint hint = choose_pivot(range.a, range.b, &pivot, context);
			if (hint == HINT_DECREASING) {
				reverse_range(range.a, range.b, context);
				pivot = (range.b - 1) - (pivot - range.a);
				hint = HINT_INCREASING;
			}
			if (range.balanced && range.partitioned && hint == HINT_INCREASING) {
				if (partial_insertion_sort(range.a, range.b, context))
					break;
			}
			if (range.a > 0 && !less_than(context, range.a - 1, pivot)) {
				range.a = partition_equal(range.a, range.b, pivot, context);
				continue;
			}
			uint32_t mid = pivot;
			bool was_partitioned = partition(range.a, range.b, &mid, context);
			uint32_t left_len = mid - range.a;
			uint32_t right_len = range.b - (mid + 1);
			bool left_is_smaller = left_len < right_len;
			uint32_t smaller_len = left_is_smaller ? left_len : right_len;
			bool was_balanced = smaller_len >= len / 8;
			uint32_t smaller_a = left_is_smaller ? range.a : mid + 1;
			uint32_t larger_a = left_is_smaller ? mid + 1 : range.a;
			uint32_t smaller_b = left_is_smaller ? mid : range.b;
			uint32_t larger_b = left_is_smaller ? range.b : mid;
			bool smaller_leftmost = left_is_smaller ? range.leftmost : false;
			bool larger_leftmost = left_is_smaller ? false : range.leftmost;
			assert(top < 32);
			stack[top++] = (Range){larger_a, larger_b, range.limit, larger_leftmost, was_balanced, was_partitioned};
			range.a = smaller_a;
			range.b = smaller_b;
			range.leftmost = smaller_leftmost;
			range.balanced = true;
			range.partitioned = was_partitioned;
		}
		if (top == 0)
			break;
		range = stack[--top];
	}
}

void
struct_layout_order(uint32_t *order, const uint32_t *alignments, uint32_t count)
{
	if (count < 2)
		return;
	SortContext context = {order, alignments};
	uint32_t limit = 0;
	for (uint32_t n = count; n > 1; n >>= 1)
		limit++;
	pdq_sort(context, count, limit);
}
