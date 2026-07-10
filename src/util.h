#ifndef NESSIE_UTIL_H
#define NESSIE_UTIL_H

#include <stddef.h>
#include <stdint.h>

#define ARRAY_LEN(ARR) (sizeof(ARR) / sizeof(ARR[0]))

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

typedef int64_t i64;
typedef int32_t i32;
typedef int16_t i16;
typedef int8_t i8;

struct span_t {
    const u8 *ptr;
    size_t len;
};

void *memdup(const void *src, size_t n);

#endif
