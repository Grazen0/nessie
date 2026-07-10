#ifndef NESSIE_ERROR_H
#define NESSIE_ERROR_H

#include "util.h"

#define NES_TRY(expr)                  \
    do {                               \
        enum nes_error_t err = (expr); \
        if (err != NES_OK)             \
            return err;                \
    } while (0)

enum nes_error_t : u8 {
    NES_OK,
    NES_ERR_BAD_MAGIC,
    NES_ERR_UNSUPPORTED_MAPPER,
    NES_ERR_TRUNCATED_ROM,
    NES_ERR_SYSTEM,
};

const char *nes_error_str(enum nes_error_t err);

#endif
