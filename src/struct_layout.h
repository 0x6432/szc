#ifndef SZC_STRUCT_LAYOUT_H
#define SZC_STRUCT_LAYOUT_H

#include <stdint.h>

/* Pinned Zig 0.17.0 auto-layout permutation. The caller initializes order[i]
 * to i; alignments[i] is a valid power of two. No allocation or shared state.
 * For count < 2, neither pointer is dereferenced (and either may be NULL). */
void struct_layout_order(uint32_t *order, const uint32_t *alignments, uint32_t count);

#endif
