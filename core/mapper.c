#include "mapper.h"
#include "error.h"
#include "ines.h"
#include "util.h"
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

static inline struct mapper_ppu_read_t ppu_read_direct(u8 value)
{
    return (struct mapper_ppu_read_t){
        .kind = MAPPER_READ_DIRECT,
        .direct_value = value,
    };
}

static inline struct mapper_ppu_read_t ppu_read_vram(u16 vram_addr)
{
    return (struct mapper_ppu_read_t){
        .kind = MAPPER_READ_VRAM,
        .vram_addr = vram_addr,
    };
}

static struct mapper_ppu_write_t ppu_write_done = {
    .kind = MAPPER_WRITE_DONE,
};

static inline struct mapper_ppu_write_t ppu_write_vram(u16 addr, u8 value)
{
    return (struct mapper_ppu_write_t){
        .kind = MAPPER_WRITE_VRAM,
        .vram = {.addr = addr, .value = value},
    };
}

struct nrom_mapper_t {
    u8 *prg_rom;
    size_t prg_rom_len;
    u8 *chr;
    size_t chr_len;
    enum ines_nt_arrangement nt_arrangement;
};

static struct nrom_mapper_t *
nrom_mapper_init(struct nrom_mapper_t *mapper,
                 const struct ines_t ines[static 1])
{
    if (mapper == nullptr)
        return nullptr;

    u8 *prg_rom = memdup(ines->prg_rom.ptr, ines->prg_rom.len);
    if (prg_rom == nullptr)
        goto err_cleanup_1;

    u8 *chr = memdup(ines->chr.ptr, ines->chr.len);
    if (chr == nullptr)
        goto err_cleanup_2;

    mapper->prg_rom = prg_rom;
    mapper->prg_rom_len = ines->prg_rom.len;
    mapper->chr = chr;
    mapper->chr_len = ines->chr.len;
    mapper->nt_arrangement = ines->nt_arrangement;

    return mapper;

err_cleanup_2:
    free(prg_rom);
err_cleanup_1:
    return nullptr;
}

[[nodiscard]] static struct nrom_mapper_t *
nrom_mapper_create(const struct ines_t ines[static 1])
{
    return nrom_mapper_init(calloc(1, sizeof(struct nrom_mapper_t)), ines);
}

static void nrom_mapper_deinit_v(void *ptr)
{
    struct nrom_mapper_t *mapper = ptr;

    free(mapper->prg_rom);
    free(mapper->chr);
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
    return nrom_mapper_peek_v(ptr, addr);
}

static void nrom_mapper_write_v(void *ptr, [[maybe_unused]] u16 addr,
                                [[maybe_unused]] u8 value)
{
    [[maybe_unused]] struct nrom_mapper_t *mapper = ptr;
}

static struct mapper_ppu_read_t nrom_mapper_read_ppu_v(void *ptr, uint(14) addr)
{
    struct nrom_mapper_t *mapper = ptr;

    if (addr < 0x2000)
        return ppu_read_direct(mapper->chr[addr]);

    if (mapper->nt_arrangement == INES_NT_ARR_HORIZONTAL)
        return ppu_read_vram(addr & 0x7FF);

    return ppu_read_vram((addr & 0x3FF) | ((addr >> 1) & 0x400));
}

static struct mapper_ppu_write_t
nrom_mapper_write_ppu_v(void *ptr, uint(14) addr, u8 value)
{
    [[maybe_unused]] struct nrom_mapper_t *mapper = ptr;

    if (addr < 0x2000)
        return ppu_write_done;

    if (mapper->nt_arrangement == INES_NT_ARR_HORIZONTAL)
        return ppu_write_vram(addr & 0x7FF, value);

    return ppu_write_vram((addr & 0x3FF) | ((addr >> 1) & 0x400), value);
}

static const struct mapper_vtable_t NROM_MAPPER_VTABLE = {
    .deinit = nrom_mapper_deinit_v,
    .read = nrom_mapper_read_v,
    .peek = nrom_mapper_peek_v,
    .write = nrom_mapper_write_v,
    .read_ppu = nrom_mapper_read_ppu_v,
    .write_ppu = nrom_mapper_write_ppu_v,
};

struct mmc1_mapper_t {
    u8 *prg_rom;
    size_t prg_rom_len;
    u8 *prg_ram;
    size_t prg_ram_len;
    u8 *chr;
    size_t chr_len;
    uint(5) shift_reg;

    union {
        struct {
            uint(5) ctrl;
            uint(5) chr_bank_0;
            uint(5) chr_bank_1;
            uint(5) prg_bank;
        };
        uint(5) regs[4];
    };
};

static void mmc1_mapper_reset(struct mmc1_mapper_t *mapper)
{
    mapper->shift_reg = 0x10;
    mapper->ctrl |= 0x0C;
}

static struct mmc1_mapper_t *
mmc1_mapper_init(struct mmc1_mapper_t *mapper,
                 const struct ines_t ines[static 1])
{
    if (mapper == nullptr)
        return nullptr;

    u8 *prg_rom = memdup(ines->prg_rom.ptr, ines->prg_rom.len);
    if (prg_rom == nullptr)
        goto err_cleanup_1;

    size_t prg_ram_len = 0x8000; // 32 KiB

    u8 *prg_ram = calloc(prg_ram_len, sizeof(prg_ram[0]));
    if (prg_ram == nullptr)
        goto err_cleanup_2;

    u8 *chr = memdup(ines->chr.ptr, ines->chr.len);
    if (chr == nullptr)
        goto err_cleanup_3;

    *mapper = (struct mmc1_mapper_t){};
    mapper->prg_rom = prg_rom;
    mapper->prg_rom_len = ines->prg_rom.len;
    mapper->prg_ram = prg_ram;
    mapper->prg_ram_len = prg_ram_len;
    mapper->chr = chr;
    mapper->chr_len = ines->chr.len;
    mmc1_mapper_reset(mapper);

    return mapper;

err_cleanup_3:
    free(prg_ram);
err_cleanup_2:
    free(prg_rom);
err_cleanup_1:
    return nullptr;
}

[[nodiscard]] static struct mmc1_mapper_t *
mmc1_mapper_create(const struct ines_t ines[static 1])
{
    return mmc1_mapper_init(calloc(1, sizeof(struct mmc1_mapper_t)), ines);
}

static void mmc1_mapper_deinit_v(void *ptr)
{
    struct mmc1_mapper_t *mapper = ptr;

    free(mapper->prg_rom);
    free(mapper->prg_ram);
    free(mapper->chr);
    *mapper = (struct mmc1_mapper_t){};
}

static u8 mmc1_mapper_peek_v(const void *ptr, u16 addr)
{
    const struct mmc1_mapper_t *mapper = ptr;

    if (addr < 0x8000) {
        u8 ram_en = mapper->prg_bank >> 4;
        if (ram_en == 0)
            return mapper->prg_ram[addr & 0x1FFF];

        return 0xFF;
    }

    u8 bank_mode = (mapper->ctrl >> 2) & 0b11;
    u8 bank = mapper->prg_bank & 0xF;

    switch (bank_mode) {
        case 0:
        case 1:
            return mapper->prg_rom[(0x8000 * (bank >> 1)) + (addr - 0x8000)];
        case 2:
            if (addr < 0xC000)
                return mapper->prg_rom[addr - 0x8000];

            return mapper->prg_rom[(0x4000 * bank) + (addr - 0xC000)];
        case 3:
            if (addr < 0xC000)
                return mapper->prg_rom[(0x4000 * bank) + (addr - 0x8000)];

            return mapper
                ->prg_rom[(mapper->prg_rom_len - 0x4000) + (addr - 0xC000)];
        default:
            unreachable();
    }
}

static u8 mmc1_mapper_read_v(void *ptr, u16 addr)
{
    return mmc1_mapper_peek_v(ptr, addr);
}

static void mmc1_mapper_write_v(void *ptr, u16 addr, u8 value)
{
    struct mmc1_mapper_t *mapper = ptr;

    if (addr < 0x8000) {
        u8 ram_en = mapper->prg_bank >> 4;

        if (ram_en == 0)
            mapper->prg_ram[addr & 0x1FFF] = value;

        return;
    }

    if ((value & 0x80) != 0) {
        mmc1_mapper_reset(mapper);
        return;
    }

    u8 prev_lsb = mapper->shift_reg & 1;
    mapper->shift_reg = (mapper->shift_reg >> 1) | ((value & 1) << 4);

    if (prev_lsb == 1) {
        mapper->regs[(addr >> 13) & 0b11] = mapper->shift_reg;
        mapper->shift_reg = 0x10;
    }
}

static struct mapper_ppu_read_t mmc1_mapper_read_ppu_v(void *ptr, uint(14) addr)
{
    struct mmc1_mapper_t *mapper = ptr;

    if (addr < 0x2000) {
        u8 bank_mode = mapper->ctrl >> 4;

        if (bank_mode == 0) {
            return ppu_read_direct(
                mapper->chr[(0x2000 * mapper->chr_bank_0) + addr]);
        }

        if (addr < 0x1000) {
            return ppu_read_direct(
                mapper->chr[(0x1000 * mapper->chr_bank_0) + addr]);
        }

        return ppu_read_direct(
            mapper->chr[(0x1000 * mapper->chr_bank_1) + (addr - 0x1000)]);
    }

    u8 nt_arr = mapper->ctrl & 0b11;

    switch (nt_arr) {
        case 0: // one screen, lower bank
            return ppu_read_vram(addr & 0x3FF);
        case 1: // one screen, upper bank
            return ppu_read_vram(0x400 + (addr & 0x3FF));
        case 2: // horizontal arrangement (vertical mirroring)
            return ppu_read_vram(addr & 0x7FF);
        case 3: // vertical arrangement (horizontal mirroring)
            return ppu_read_vram((addr & 0x3FF) | ((addr >> 1) & 0x400));
        default:
            unreachable();
    }
}

static struct mapper_ppu_write_t
mmc1_mapper_write_ppu_v(void *ptr, uint(14) addr, u8 value)
{
    [[maybe_unused]] struct mmc1_mapper_t *mapper = ptr;

    if (addr < 0x2000)
        return ppu_write_done;

    u8 nt_arr = mapper->ctrl & 0b11;

    switch (nt_arr) {
        case 0: // one screen, lower bank
            return ppu_write_vram(addr & 0x3FF, value);
        case 1: // one screen, upper bank
            return ppu_write_vram(0x400 + (addr & 0x3FF), value);
        case 2: // horizontal arrangement (vertical mirroring)
            return ppu_write_vram(addr & 0x7FF, value);
        case 3: // vertical arrangement (horizontal mirroring)
            return ppu_write_vram((addr & 0x3FF) | ((addr >> 1) & 0x400),
                                  value);
        default:
            unreachable();
    }
}

static struct mapper_vtable_t MMC1_MAPPER_VTABLE = {
    .deinit = mmc1_mapper_deinit_v,
    .read = mmc1_mapper_read_v,
    .peek = mmc1_mapper_peek_v,
    .write = mmc1_mapper_write_v,
    .read_ppu = mmc1_mapper_read_ppu_v,
    .write_ppu = mmc1_mapper_write_ppu_v,
};

#define MAPPER_CASE(NUM, CREATE, VTABLE) \
    case NUM:                            \
        out_mapper->ptr = CREATE(ines);  \
        out_mapper->vtable = &VTABLE;    \
        break;

enum nes_error_t mapper_from_rom(const struct ines_t ines[static 1],
                                 struct mapper_t *out_mapper)
{

    switch (ines->mapper_num) {
        MAPPER_CASE(0x00, nrom_mapper_create, NROM_MAPPER_VTABLE)
        MAPPER_CASE(0x01, mmc1_mapper_create, MMC1_MAPPER_VTABLE)

        default:
            return NES_ERR_UNSUPPORTED_MAPPER;
    }

    if (out_mapper->ptr == nullptr)
        return NES_ERR_SYSTEM;

    return NES_OK;
}
