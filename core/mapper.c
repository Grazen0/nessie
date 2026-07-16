#include "mapper.h"
#include "error.h"
#include "ines.h"
#include "util.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

struct nrom_mapper_t {
    u8 *prg_rom;
    size_t prg_rom_len;
    u8 *chr_rom;
    size_t chr_rom_len;
};

static struct nrom_mapper_t *
nrom_mapper_init(struct nrom_mapper_t *mapper, size_t prg_rom_len,
                 const u8 prg_rom[static prg_rom_len], size_t chr_rom_len,
                 const u8 chr_rom[static chr_rom_len])
{
    if (mapper == nullptr)
        return nullptr;

    u8 *prg_rom_dup = memdup(prg_rom, prg_rom_len);
    if (prg_rom_dup == nullptr)
        goto err_cleanup_1;

    u8 *chr_rom_dup = memdup(chr_rom, chr_rom_len);
    if (chr_rom_dup == nullptr)
        goto err_cleanup_2;

    *mapper = (struct nrom_mapper_t){
        .prg_rom = prg_rom_dup,
        .prg_rom_len = prg_rom_len,
        .chr_rom = chr_rom_dup,
        .chr_rom_len = chr_rom_len,
    };

    return mapper;

err_cleanup_2:
    free(prg_rom_dup);
err_cleanup_1:
    return nullptr;
}

[[nodiscard]] static struct nrom_mapper_t *
nrom_mapper_create(size_t prg_rom_len, const u8 prg_rom[static prg_rom_len],
                   size_t chr_rom_len, const u8 chr_rom[static chr_rom_len])
{
    return nrom_mapper_init(calloc(1, sizeof(struct nrom_mapper_t)),
                            prg_rom_len, prg_rom, chr_rom_len, chr_rom);
}

static void nrom_mapper_deinit_v(void *ptr)
{
    struct nrom_mapper_t *mapper = ptr;

    free(mapper->prg_rom);
    free(mapper->chr_rom);
    *mapper = (struct nrom_mapper_t){};
}

static u8 nrom_mapper_peek_v(const void *ptr, u16 addr)
{
    const struct nrom_mapper_t *mapper = ptr;

    if (addr < 0x8000)
        return 0xFF;

    return mapper->prg_rom[(addr - 0x8000) % mapper->prg_rom_len];
}

static u8 nrom_mapper_read_v(void *ptr, u16 addr)
{
    const struct nrom_mapper_t *mapper = ptr;

    if (addr < 0x8000)
        return 0xFF;

    return mapper->prg_rom[(addr - 0x8000) % mapper->prg_rom_len];
}

static void nrom_mapper_write_v(void *ptr, [[maybe_unused]] u16 addr,
                                [[maybe_unused]] u8 value)
{
    [[maybe_unused]] struct nrom_mapper_t *mapper = ptr;
}

static struct mapper_ppu_read_t nrom_mapper_read_ppu_v(void *ptr, uint(14) addr)
{
    struct nrom_mapper_t *mapper = ptr;

    struct mapper_ppu_read_t result = {};

    assert(addr < 0x3000);

    if (addr < 0x2000) {
        result.kind = MAPPER_READ_DIRECT;
        result.direct_value = mapper->chr_rom[addr];
    } else {
        result.kind = MAPPER_READ_VRAM;
        result.vram_addr =
            (addr - 0x2000) % 0x800; // TODO: come back to mirroring later
    }

    return result;
}

static struct mapper_ppu_write_t
nrom_mapper_write_ppu_v(void *ptr, uint(14) addr, u8 value)
{
    [[maybe_unused]] struct nrom_mapper_t *mapper = ptr;

    struct mapper_ppu_write_t result = {};

    assert(addr < 0x3000);

    if (addr < 0x2000) {
        result.kind = MAPPER_WRITE_DONE;
    } else {
        result.kind = MAPPER_WRITE_VRAM;
        result.vram.addr = (addr - 0x2000) % 0x800;
        result.vram.value = value;
    }

    return result;
}

static const struct mapper_vtable_t NROM_MAPPER_VTABLE = {
    .deinit = nrom_mapper_deinit_v,
    .read = nrom_mapper_read_v,
    .peek = nrom_mapper_peek_v,
    .write = nrom_mapper_write_v,
    .read_ppu = nrom_mapper_read_ppu_v,
    .write_ppu = nrom_mapper_write_ppu_v,
};

enum nes_error_t mapper_from_rom(const struct ines_t ines[static 1],
                                 struct mapper_t *out_mapper)
{

    switch (ines->mapper_num) {
        case 0x00:
            struct nrom_mapper_t *mapper =
                nrom_mapper_create(ines->prg_rom.len, ines->prg_rom.ptr,
                                   ines->chr_rom.len, ines->chr_rom.ptr);
            if (mapper == nullptr)
                return NES_ERR_SYSTEM;

            out_mapper->ptr = mapper;
            out_mapper->vtable = &NROM_MAPPER_VTABLE;
            break;
        default:
            return NES_ERR_UNSUPPORTED_MAPPER;
    }

    return NES_OK;
}
