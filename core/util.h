#ifndef NESSIE_UTIL_H
#define NESSIE_UTIL_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define ARRAY_LEN(ARR) (sizeof(ARR) / sizeof(ARR[0]))

#define PANIC(...)                                                          \
    do {                                                                    \
        __VA_OPT__(fprintf(stderr, "panic (%s:%d): ", __FILE__, __LINE__);) \
        __VA_OPT__(if (0))                                                  \
        fprintf(stderr, "panic (%s:%d)", __FILE__, __LINE__);               \
        __VA_OPT__(fprintf(stderr, __VA_ARGS__);)                           \
        fputc('\n', stderr);                                                \
        abort();                                                            \
    } while (0)

#define ASSERT(COND, ...) \
    if (!(COND))          \
    PANIC(__VA_ARGS__)

#define uint(n) unsigned _BitInt(n)

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

typedef int64_t i64;
typedef int32_t i32;
typedef int16_t i16;
typedef int8_t i8;

void *memdup(const void *src, size_t n);

struct view_t {
    const u8 *ptr;
    size_t len;
};

struct span_t {
    u8 *ptr;
    size_t len;
};

[[nodiscard]] struct span_t span_alloc(size_t len);

[[nodiscard]] struct span_t span_alloc_uninit(size_t len);

[[nodiscard]] struct span_t span_dup(struct view_t view);

void span_deinit(struct span_t *span);

static inline void set_bits(u8 num[static 1], u8 mask, bool value)
{
    if (value)
        *num |= mask;
    else
        *num &= ~mask;
}

// Credit: https://stackoverflow.com/a/2602885/14766637
static inline u8 reverse_bits(u8 b)
{
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
    return b;
}

#endif
