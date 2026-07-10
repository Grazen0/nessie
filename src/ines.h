#ifndef NESSIE_INES_H
#define NESSIE_INES_H

#include "error.h"
#include "util.h"
#include <stddef.h>

static constexpr size_t INES_PRG_BANK_LEN = 0x4000;
static constexpr size_t INES_CHR_BANK_LEN = 0x2000;

struct ines_t {
    struct span_t prg_rom;
    struct span_t chr_rom;
    struct span_t trainer;
    u8 prg_banks;
    u8 chr_banks;
    u8 mapper_num;
};

// contains data borrowed from rom
[[nodiscard]] enum nes_error_t ines_parse(size_t rom_len,
                                          const u8 rom[static rom_len],
                                          struct ines_t *out_ines);

#endif
