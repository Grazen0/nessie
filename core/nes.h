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

static constexpr size_t NES_MASTER_CLK_FREQ = 21'477'272;

static constexpr size_t NES_SCREEN_WIDTH = 256;
static constexpr size_t NES_SCREEN_HEIGHT = 240;
static constexpr float NES_SCREEN_RATIO =
    (float)NES_SCREEN_WIDTH / NES_SCREEN_HEIGHT;

static constexpr size_t NES_PAL_RAM_SIZE = 32;

struct nes_btns_t {
    bool a;
    bool b;
    bool select;
    bool start;
    bool up;
    bool down;
    bool left;
    bool right;
};

struct nes_t {
    struct cpu_t cpu;
    struct mapper_t mapper;
    struct nes_btns_t btns;
    size_t px;
    size_t py;
    u8 *ram;
    u8 *vram;
    u8 (*scanout_buf)[NES_SCREEN_WIDTH];
    u16 ppuaddr;
    u8 pal_ram[NES_PAL_RAM_SIZE];
    u8 ppuctrl;
    u8 ppumask;
    u8 ppustatus;
    u8 oamaddr;
    u8 oamdata;
    u8 ppuscroll;
    u8 ppudata_buf;
    u8 oamdma;
    u8 joy_strobe;
    u8 joy1;
    u8 joy2;
    unsigned _BitInt(1) w;
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

u64 nes_dispatch_pixel(struct nes_t nes[static 1]);

u8 nes_read_ppu(struct nes_t nes[static 1], u16 addr);

#endif
