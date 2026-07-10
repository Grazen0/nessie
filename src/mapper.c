#include "mapper.h"
#include "error.h"
#include "ines.h"
#include "util.h"
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

void mapper_deinit(struct mapper_t mapper)
{
    mapper.vtable->deinit(mapper.ptr);
    free(mapper.ptr);
}

u8 mapper_read(struct mapper_t mapper, u16 addr)
{
    return mapper.vtable->read(mapper.ptr, addr);
}

void mapper_write(struct mapper_t mapper, u16 addr, u8 value)
{
    mapper.vtable->write(mapper.ptr, addr, value);
}

u8 mapper_read_ppu(struct mapper_t mapper, u16 addr)
{
    return mapper.vtable->read_ppu(mapper.ptr, addr);
}

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
    if (mapper != nullptr) {
        u8 *prg_rom_dup = memdup(prg_rom, prg_rom_len);
        if (prg_rom_dup == nullptr)
            return nullptr;

        u8 *chr_rom_dup = memdup(chr_rom, chr_rom_len);
        if (chr_rom_dup == nullptr) {
            free(prg_rom_dup);
            return nullptr;
        }

        *mapper = (struct nrom_mapper_t){
            .prg_rom = prg_rom_dup,
            .prg_rom_len = prg_rom_len,
            .chr_rom = chr_rom_dup,
            .chr_rom_len = chr_rom_len,
        };
    }

    return mapper;
}

static struct nrom_mapper_t *
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

static u8 nrom_mapper_read_v(void *ptr, u16 addr)
{
    struct nrom_mapper_t *mapper = ptr;

    assert(addr >= 0x8000);
    return mapper->prg_rom[(addr - 0x8000) % mapper->prg_rom_len];
}

static void nrom_mapper_write_v(void *ptr, [[maybe_unused]] u16 addr,
                                [[maybe_unused]] u8 value)
{
    [[maybe_unused]] struct nrom_mapper_t *mapper = ptr;
}

static u8 nrom_mapper_read_ppu_v(void *ptr, u16 addr)
{
    struct nrom_mapper_t *mapper = ptr;

    assert(addr < 0x2000);
    return mapper->chr_rom[addr];
}

static const struct mapper_vtable_t NROM_MAPPER_VTABLE = {
    .deinit = nrom_mapper_deinit_v,
    .read = nrom_mapper_read_v,
    .write = nrom_mapper_write_v,
    .read_ppu = nrom_mapper_read_ppu_v,
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
