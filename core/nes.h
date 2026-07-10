#ifndef NESSIE_NES_H
#define NESSIE_NES_H

#include "cpu.h"
#include "error.h"
#include "ines.h"
#include "mapper.h"
#include "util.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

static constexpr size_t NES_RAM_SIZE = 0x800;

static constexpr size_t NES_CLK_FREQ_HZ = 1'789'773;

struct nes_t {
    struct cpu_t cpu;
    struct mapper_t mapper;
    u8 *ram;
    u8 ppuctrl;
    u8 ppumask;
    u8 ppustatus;
    u8 oamaddr;
    u8 oamdata;
    u8 ppuscroll;
    u8 ppuaddr;
    u8 ppudata;
    u8 oamdma;
    u8 joy1;
    u8 joy2;
    bool mapper_init;
};

struct nes_t *nes_init(struct nes_t *nes);

struct nes_t *nes_create();

void nes_deinit(struct nes_t *nes);

void nes_destroy(struct nes_t *nes);

[[nodiscard]] enum nes_error_t nes_load_rom(struct nes_t nes[static 1],
                                            const struct ines_t ines[static 1]);

void nes_reset(struct nes_t nes[static 1]);

u64 nes_dispatch_cpu(struct nes_t nes[static 1]);

#endif
