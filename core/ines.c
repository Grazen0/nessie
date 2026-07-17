#include "ines.h"
#include "error.h"
#include "util.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static constexpr size_t INES_HEADER_LEN = 16;

static const u8 INES_MAGIC[] = {0x4E, 0x45, 0x53, 0x1A};

enum flags6_t {
    F6_NT_ARRANGEMENT = 1 << 0,
    F6_BATTERY = 1 << 1,
    F6_TRAINER = 1 << 2,
    F6_ALT_NT = 1 << 3,
};

enum nes_error_t span_take(struct view_t span[static 1], size_t size,
                           struct view_t *out_span)
{
    if (span->len < size)
        return NES_ERR_TRUNCATED_ROM;

    if (out_span != nullptr) {
        out_span->ptr = span->ptr;
        out_span->len = size;
    }

    span->ptr += size;
    span->len -= size;
    return NES_OK;
}

enum nes_error_t ines_parse(size_t rom_len, const u8 rom_data[static rom_len],
                            struct ines_t *out_ines)
{
    struct view_t reader = {rom_data, rom_len};

    struct view_t header = {};
    NES_TRY(span_take(&reader, INES_HEADER_LEN, &header));

    if (memcmp(header.ptr, INES_MAGIC, ARRAY_LEN(INES_MAGIC)) != 0)
        return NES_ERR_BAD_MAGIC;

    u8 prg_banks = header.ptr[4];
    u8 chr_banks = header.ptr[5];
    u8 flags_6 = header.ptr[6];
    u8 flags_7 = header.ptr[7];

    struct view_t trainer = {};

    if ((flags_6 & F6_TRAINER) != 0) {
        static constexpr size_t TRAINER_LEN = 512;
        NES_TRY(span_take(&reader, TRAINER_LEN, &trainer));
    }

    struct view_t prg_rom = {};
    struct view_t chr = {};

    NES_TRY(span_take(&reader, INES_PRG_BANK_LEN * prg_banks, &prg_rom));
    NES_TRY(span_take(&reader, INES_CHR_BANK_LEN * chr_banks, &chr));

    if (out_ines != nullptr) {
        *out_ines = (struct ines_t){
            .prg_rom = prg_rom,
            .chr = chr,
            .trainer = trainer,
            .prg_banks = prg_banks,
            .chr_banks = chr_banks,
            .mapper_num = (flags_7 & 0xF0) | ((flags_6 >> 4) & 0x0F),
            .nt_arrangement = (flags_6 & F6_NT_ARRANGEMENT) == 0
                                  ? INES_NT_ARR_VERTICAL
                                  : INES_NT_ARR_HORIZONTAL,
        };
    }
    return NES_OK;
}
