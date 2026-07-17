#ifndef NESSIE_INES_H
#define NESSIE_INES_H

#include "error.h"
#include "util.h"
#include <stddef.h>

static constexpr size_t INES_PRG_BANK_LEN = 0x4000;
static constexpr size_t INES_CHR_BANK_LEN = 0x2000;

enum ines_nt_arrangement : bool {
    INES_NT_ARR_VERTICAL,
    INES_NT_ARR_HORIZONTAL,
};

struct ines_t {
    struct view_t prg_rom;
    struct view_t chr;
    struct view_t trainer;
    u8 prg_banks;
    u8 chr_banks;
    u8 mapper_num;
    enum ines_nt_arrangement nt_arrangement;
};

// contains data borrowed from rom
[[nodiscard]] enum nes_error_t ines_parse(size_t rom_len,
                                          const u8 rom[static rom_len],
                                          struct ines_t *out_ines);

#endif
