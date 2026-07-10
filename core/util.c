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
