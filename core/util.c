#include "util.h"
#include <stdlib.h>
#include <string.h>

void *memdup(const void *src, size_t n)
{
    u8 *dup = malloc(n);

    if (dup != nullptr)
        memcpy(dup, src, n);

    return dup;
}

struct span_t span_alloc(size_t len)
{
    u8 *ptr = calloc(len, sizeof(ptr[0]));
    ASSERT(ptr != nullptr);

    return (struct span_t){
        .ptr = ptr,
        .len = len,
    };
}

struct span_t span_alloc_uninit(size_t len)
{
    u8 *ptr = malloc(len);
    ASSERT(ptr != nullptr);

    return (struct span_t){
        .ptr = ptr,
        .len = len,
    };
}

struct span_t span_dup(struct view_t view)
{
    u8 *ptr = memdup(view.ptr, view.len);
    ASSERT(ptr != nullptr);

    return (struct span_t){
        .ptr = ptr,
        .len = view.len,
    };
}

void span_deinit(struct span_t *span)
{
    if (span)
        free(span->ptr);

    *span = (struct span_t){};
}
