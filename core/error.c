#include "error.h"
#include <stddef.h>

const char *nes_error_str(enum nes_error_t err)
{
    switch (err) {
        case NES_OK:
            return "Ok";
        case NES_ERR_BAD_MAGIC:
            return "Invalid iNES magic number";
        case NES_ERR_UNSUPPORTED_MAPPER:
            return "Unsupported mapper number";
        case NES_ERR_TRUNCATED_ROM:
            return "Not enough bytes in ROM";
        case NES_ERR_SYSTEM:
            return "System error";
    }

    unreachable();
}
