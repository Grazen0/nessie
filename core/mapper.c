#include "mapper.h"
#include "error.h"
#include "ines.h"
#include "util.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

static inline struct mapper_ppu_read_t ppu_read_direct(u8 value)
{
    return (struct mapper_ppu_read_t){
        .kind = MAPPER_PPU_READ_DIRECT,
        .direct_value = value,
    };
}

static inline struct mapper_ppu_read_t ppu_read_vram(u16 vram_addr)
{
    return (struct mapper_ppu_read_t){
        .kind = MAPPER_PPU_READ_VRAM,
        .vram_addr = vram_addr,
    };
}

static struct mapper_ppu_write_t ppu_write_done = {
    .kind = MAPPER_PPU_WRITE_DONE,
};

static inline struct mapper_ppu_write_t ppu_write_vram(u16 addr, u8 value)
{
    return (struct mapper_ppu_write_t){
        .kind = MAPPER_PPU_WRITE_VRAM,
        .vram = {.addr = addr, .value = value},
    };
}

struct nrom_mapper_t {
    struct span_t prg_rom;
    struct span_t chr_rom;
    enum ines_nt_arrangement nt_arrangement;
};

static struct nrom_mapper_t *
nrom_mapper_init(struct nrom_mapper_t *mapper,
                 const struct ines_t ines[static 1])
{
    if (mapper == nullptr)
        return nullptr;

    auto prg_rom = span_dup(ines->prg_rom);
    auto chr_rom = span_dup(ines->chr_rom);

    mapper->prg_rom = prg_rom;
    mapper->chr_rom = chr_rom;
    mapper->nt_arrangement = ines->nt_arrangement;

    return mapper;
}

[[nodiscard]] static struct nrom_mapper_t *
nrom_mapper_create(const struct ines_t ines[static 1])
{
    return nrom_mapper_init(calloc(1, sizeof(struct nrom_mapper_t)), ines);
}

static void nrom_mapper_deinit_v(void *ptr)
{
    struct nrom_mapper_t *mapper = ptr;

    span_deinit(&mapper->prg_rom);
    span_deinit(&mapper->chr_rom);
    *mapper = (struct nrom_mapper_t){};
}

static u8 nrom_mapper_peek_v(const void *ptr, u16 addr)
{
    const struct nrom_mapper_t *mapper = ptr;

    if (addr < 0x8000)
        return 0xFF;

    return mapper->prg_rom.ptr[(addr - 0x8000) % mapper->prg_rom.len];
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
        return ppu_read_direct(mapper->chr_rom.ptr[addr]);

    if (mapper->nt_arrangement == INES_NT_ARR_HORIZONTAL)
        return ppu_read_vram(addr & 0x7FF);

    return ppu_read_vram((addr & 0x3FF) | ((addr >> 1) & 0x400));
}

static struct mapper_ppu_write_t
nrom_mapper_write_ppu_v(void *ptr, uint(14) addr, u8 value)
{
    struct nrom_mapper_t *mapper = ptr;

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
    struct span_t prg_rom;
    struct span_t prg_ram;
    struct span_t chr;
    bool uses_chr_ram;
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
    auto prg_rom = span_dup(ines->prg_rom);

    // according to the wiki, no program should use more than 32 kib of prg ram
    auto prg_ram = span_alloc(0x8000);

    bool uses_chr_ram = ines->chr_rom.len == 0;
    struct span_t chr =
        uses_chr_ram ? span_alloc(0x2000) : span_dup(ines->chr_rom);

    *mapper = (struct mmc1_mapper_t){};
    mapper->prg_rom = prg_rom;
    mapper->prg_ram = prg_ram;
    mapper->chr = chr;
    mapper->uses_chr_ram = uses_chr_ram;
    mmc1_mapper_reset(mapper);

    return mapper;
}

[[nodiscard]] static struct mmc1_mapper_t *
mmc1_mapper_create(const struct ines_t ines[static 1])
{
    return mmc1_mapper_init(calloc(1, sizeof(struct mmc1_mapper_t)), ines);
}

static void mmc1_mapper_deinit_v(void *ptr)
{
    struct mmc1_mapper_t *mapper = ptr;

    span_deinit(&mapper->chr);
    span_deinit(&mapper->prg_ram);
    span_deinit(&mapper->prg_rom);
    *mapper = (struct mmc1_mapper_t){};
}

static u8 mmc1_mapper_peek_v(const void *ptr, u16 addr)
{
    const struct mmc1_mapper_t *mapper = ptr;

    if (addr < 0x8000) {
        u8 ram_en = mapper->prg_bank >> 4;
        if (ram_en == 0)
            return mapper->prg_ram.ptr[addr & 0x1FFF];

        return 0xFF;
    }

    u8 bank_mode = (mapper->ctrl >> 2) & 0b11;
    u8 bank = mapper->prg_bank & 0xF;

    switch (bank_mode) {
        case 0:
        case 1:
            return mapper->prg_rom.ptr[((bank >> 1) << 15) | (addr & 0x7FFF)];
        case 2:
            if (addr < 0xC000)
                return mapper->prg_rom.ptr[addr & 0x7FFF];

            return mapper->prg_rom.ptr[(bank << 14) | (addr & 0x3FFF)];
        case 3:
            if (addr < 0xC000)
                return mapper->prg_rom.ptr[(bank << 14) | (addr & 0x3FFF)];

            return mapper->prg_rom
                .ptr[(mapper->prg_rom.len - 0x4000) | (addr & 0x3FFF)];
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
            mapper->prg_ram.ptr[addr & 0x1FFF] = value;

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

static size_t mmc1_map_chr_addr(const struct mmc1_mapper_t mapper[static 1],
                                uint(14) addr)
{
    assert(addr < 0x2000);

    u8 bank_mode = mapper->ctrl >> 4;
    size_t unwrapped_addr = 0;

    if (bank_mode == 0)
        unwrapped_addr = (0x2000 * mapper->chr_bank_0) | addr;
    else if (addr < 0x1000)
        unwrapped_addr = (0x1000 * mapper->chr_bank_0) | addr;
    else
        unwrapped_addr = (0x1000 * mapper->chr_bank_1) | (addr & 0xFFF);

    return unwrapped_addr % mapper->chr.len;
}

static u16 mmc1_map_vram_addr(struct mmc1_mapper_t mapper[static 1], u16 addr)
{
    assert(addr >= 0x2000);

    u8 nt_arr = mapper->ctrl & 0b11;
    switch (nt_arr) {
        case 0: // one screen, lower bank
            return addr & 0x3FF;
        case 1: // one screen, upper bank
            return 0x400 | (addr & 0x3FF);
        case 2: // horizontal arrangement (vertical mirroring)
            return addr & 0x7FF;
        case 3: // vertical arrangement (horizontal mirroring)
            return (addr & 0x3FF) | ((addr >> 1) & 0x400);
        default:
            unreachable();
    }
}

static struct mapper_ppu_read_t mmc1_mapper_read_ppu_v(void *ptr, uint(14) addr)
{
    struct mmc1_mapper_t *mapper = ptr;

    if (addr < 0x2000) {
        size_t chr_addr = mmc1_map_chr_addr(mapper, addr);
        return ppu_read_direct(mapper->chr.ptr[chr_addr]);
    }

    u16 vram_addr = mmc1_map_vram_addr(mapper, addr);
    return ppu_read_vram(vram_addr);
}

static struct mapper_ppu_write_t
mmc1_mapper_write_ppu_v(void *ptr, uint(14) addr, u8 value)
{
    struct mmc1_mapper_t *mapper = ptr;

    if (addr < 0x2000) {
        size_t chr_addr = mmc1_map_chr_addr(mapper, addr);
        mapper->chr.ptr[chr_addr] = value;
        return ppu_write_done;
    }

    u16 vram_addr = mmc1_map_vram_addr(mapper, addr);
    return ppu_write_vram(vram_addr, value);
}

static struct mapper_vtable_t MMC1_MAPPER_VTABLE = {
    .deinit = mmc1_mapper_deinit_v,
    .read = mmc1_mapper_read_v,
    .peek = mmc1_mapper_peek_v,
    .write = mmc1_mapper_write_v,
    .read_ppu = mmc1_mapper_read_ppu_v,
    .write_ppu = mmc1_mapper_write_ppu_v,
};

struct mmc2_mapper_t {
    struct span_t prg_rom;
    struct span_t chr_rom;
    uint(4) prg_rom_bank;
    uint(5) chr_rom_bank[4];
    uint(1) mirroring;
    u8 latch[2];
};

static struct mmc2_mapper_t *
mmc2_mapper_init(struct mmc2_mapper_t *mapper,
                 const struct ines_t ines[static 1])
{
    if (mapper == nullptr)
        return nullptr;

    struct span_t prg_rom = span_dup(ines->prg_rom);
    struct span_t chr_rom = span_dup(ines->chr_rom);

    *mapper = (struct mmc2_mapper_t){};
    mapper->prg_rom = prg_rom;
    mapper->chr_rom = chr_rom;

    return mapper;
}

[[nodiscard]] static struct mmc2_mapper_t *
mmc2_mapper_create(const struct ines_t ines[static 1])
{
    return mmc2_mapper_init(calloc(1, sizeof(struct mmc2_mapper_t)), ines);
}

static void mmc2_mapper_deinit_v(void *ptr)
{
    struct mmc2_mapper_t *mapper = ptr;

    span_deinit(&mapper->chr_rom);
    span_deinit(&mapper->prg_rom);
    *mapper = (struct mmc2_mapper_t){};
}

static u8 mmc2_mapper_peek_v(const void *ptr, u16 addr)
{
    const struct mmc2_mapper_t *mapper = ptr;

    if (addr < 0x8000)
        return 0xFF;

    if (addr < 0xA000) {
        size_t rom_addr =
            (0x2000 * (size_t)mapper->prg_rom_bank) | (addr & 0x1FFF);
        return mapper->prg_rom.ptr[rom_addr];
    }

    // static int i = 0;
    // if (++i >= 10)
    //     exit(0);

    size_t rom_addr = mapper->prg_rom.len - 0x8000 + (addr & 0x7FFF);
    return mapper->prg_rom.ptr[rom_addr];
}

static u8 mmc2_mapper_read_v(void *ptr, u16 addr)
{
    return mmc2_mapper_peek_v(ptr, addr);
}

static void mmc2_mapper_write_v(void *ptr, u16 addr, u8 value)
{
    struct mmc2_mapper_t *mapper = ptr;

    if (addr < 0xA000)
        return;

    if (addr < 0xB000) {
        mapper->prg_rom_bank = value;
        return;
    }

    if (addr < 0xF000) {
        mapper->chr_rom_bank[(addr >> 12) - 0xB] = value;
        return;
    }

    mapper->mirroring = value;
}

static struct mapper_ppu_read_t mmc2_mapper_read_ppu_v(void *ptr, uint(14) addr)
{
    struct mmc2_mapper_t *mapper = ptr;

    if (addr < 0x2000) {
        size_t bank_num = 0;

        if (addr < 0x1000)
            bank_num = mapper->latch[0] == 0xFD ? 0 : 1;
        else
            bank_num = mapper->latch[1] == 0xFD ? 2 : 3;

        size_t bank = mapper->chr_rom_bank[bank_num];

        u8 val =
            mapper->chr_rom
                .ptr[((0x1000 * bank) | (addr & 0xFFF)) % mapper->chr_rom.len];

        if (addr == 0x0FD8)
            mapper->latch[0] = 0xFD;
        else if (addr == 0x0FE8)
            mapper->latch[0] = 0xFE;
        else if (0x1FD8 <= addr && addr <= 0x1FDF)
            mapper->latch[1] = 0xFD;
        else if (0x1FE8 <= addr && addr <= 0x1FEF)
            mapper->latch[1] = 0xFE;

        return ppu_read_direct(val);
    }

    if (mapper->mirroring == 0) // vertical mirroring
        return ppu_read_vram(addr & 0x7FF);

    // horizontal mirroring
    return ppu_read_vram((addr & 0x3FF) | ((addr >> 1) & 0x400));
}

static struct mapper_ppu_write_t
mmc2_mapper_write_ppu_v(void *ptr, [[maybe_unused]] uint(14) addr, u8 value)
{
    struct mmc2_mapper_t *mapper = ptr;

    if (addr < 0x2000)
        return ppu_write_done;

    if (mapper->mirroring == 0) // vertical mirroring
        return ppu_write_vram(addr & 0x7FF, value);

    // horizontal mirroring
    return ppu_write_vram((addr & 0x3FF) | ((addr >> 1) & 0x400), value);
}

static struct mapper_vtable_t MMC2_MAPPER_VTABLE = {
    .deinit = mmc2_mapper_deinit_v,
    .read = mmc2_mapper_read_v,
    .peek = mmc2_mapper_peek_v,
    .write = mmc2_mapper_write_v,
    .read_ppu = mmc2_mapper_read_ppu_v,
    .write_ppu = mmc2_mapper_write_ppu_v,
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
        MAPPER_CASE(0x09, mmc2_mapper_create, MMC2_MAPPER_VTABLE)

        default:
            return NES_ERR_UNSUPPORTED_MAPPER;
    }

    if (out_mapper->ptr == nullptr)
        return NES_ERR_SYSTEM;

    return NES_OK;
}
