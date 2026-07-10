#include "nes.h"
#include "cpu.h"
#include "error.h"
#include "ines.h"
#include "mapper.h"
#include <stdlib.h>

static u8 nes_read_mem(struct nes_t nes[static 1], u16 addr)
{
    if (addr < 0x2000)
        return nes->ram[addr % NES_RAM_SIZE];

    if (addr < 0x4000) {
        switch ((addr - 0x2000) % 8) {
            case 0:
                return nes->ppuctrl;
            case 1:
                return nes->ppumask;
            case 2:
                return nes->ppustatus;
            case 3:
                return nes->oamaddr;
            case 4:
                return nes->oamdata;
            case 5:
                return nes->ppuscroll;
            case 6:
                return nes->ppuaddr;
            case 7:
                return nes->ppudata;
        }
    }

    if (addr < 0x4018) {
        switch (addr - 0x4000) {
            case 0x14:
                return nes->oamdma;
            case 0x16:
                return nes->joy1;
            case 0x17:
                return nes->joy2;
        }
    }

    if (addr < 0x4020)
        return 0xFF;

    return mapper_read(nes->mapper, addr);
}

static void nes_write_mem(struct nes_t nes[static 1], u16 addr, u8 value)
{
    if (addr < 0x2000) {
        nes->ram[addr % NES_RAM_SIZE] = value;
        return;
    }

    if (addr < 0x4000) {
        switch ((addr - 0x2000) % 8) {
            case 0:
                nes->ppuctrl = value;
                return;
            case 1:
                nes->ppumask = value;
                return;
            case 2:
                nes->ppustatus = value;
                return;
            case 3:
                nes->oamaddr = value;
                return;
            case 4:
                nes->oamdata = value;
                return;
            case 5:
                nes->ppuscroll = value;
                return;
            case 6:
                nes->ppuaddr = value;
                return;
            case 7:
                nes->ppudata = value;
                return;
        }
    }

    if (addr < 0x4018) {
        switch (addr - 0x4000) {
            case 0x14:
                nes->oamdma = value;
                return;
            case 0x16:
                nes->joy1 = value;
                return;
            case 0x17:
                nes->joy2 = value;
                return;
        }
    }

    if (addr < 0x4020)
        return;

    mapper_write(nes->mapper, addr, value);
}

struct nes_t *nes_init(struct nes_t *nes)
{
    if (nes != nullptr) {
        u8 *ram = calloc(NES_RAM_SIZE, sizeof(ram[0]));
        if (ram == nullptr)
            return nullptr;

        *nes = (struct nes_t){
            .cpu = cpu_init(),
            .mapper = {},
            .mapper_init = false,
            .ram = ram,
            .ppuctrl = 0,
            .ppumask = 0,
            .ppustatus = 0,
            .oamaddr = 0,
            .oamdata = 0,
            .ppuscroll = 0,
            .ppuaddr = 0,
            .ppudata = 0,
            .oamdma = 0,
            .joy1 = 0,
            .joy2 = 0,
        };
    }

    return nes;
}

struct nes_t *nes_create()
{
    return nes_init(calloc(1, sizeof(struct nes_t)));
}

void nes_deinit(struct nes_t *nes)
{
    if (nes != nullptr) {
        mapper_deinit(nes->mapper);
        free(nes->ram);
        *nes = (struct nes_t){};
    }
}

void nes_destroy(struct nes_t *nes)
{
    nes_deinit(nes);
    free(nes);
}

enum nes_error_t nes_load_rom(struct nes_t nes[static 1],
                              const struct ines_t ines[static 1])
{
    if (nes->mapper_init) {
        mapper_deinit(nes->mapper);
        nes->mapper_init = false;
    }

    NES_TRY(mapper_from_rom(ines, &nes->mapper));

    nes->mapper_init = true;

    return NES_OK;
}

static void nes_deinit_v(void *ptr)
{
    nes_deinit(ptr);
}

static u8 nes_read_mem_v(void *ptr, u16 addr)
{
    return nes_read_mem(ptr, addr);
}

static void nes_write_mem_v(void *ptr, u16 addr, u8 value)
{
    nes_write_mem(ptr, addr, value);
}

static const struct memory_vtable_t NES_MEMORY_VTABLE = {
    .deinit = nes_deinit_v,
    .read = nes_read_mem_v,
    .write = nes_write_mem_v,
};

static struct memory_t nes_as_memory(struct nes_t nes[static 1])
{
    return (struct memory_t){
        .ptr = nes,
        .vtable = &NES_MEMORY_VTABLE,
    };
}

void nes_reset(struct nes_t nes[static 1])
{
    struct memory_t mem = nes_as_memory(nes);
    cpu_reset(&nes->cpu, mem);
}
