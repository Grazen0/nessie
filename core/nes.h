#ifndef NESSIE_NES_H
#define NESSIE_NES_H

#include "error.h"
#include "ines.h"
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

enum nes_btn_t : u8 {
    NES_BTN_A,
    NES_BTN_B,
    NES_BTN_SELECT,
    NES_BTN_START,
    NES_BTN_UP,
    NES_BTN_DOWN,
    NES_BTN_LEFT,
    NES_BTN_RIGHT,
};

struct nes_t;

struct nes_t *nes_init(struct nes_t *nes, FILE *log_file);

[[nodiscard]] struct nes_t *nes_create(FILE *log_file);

void nes_deinit(struct nes_t *nes);

void nes_destroy(struct nes_t *nes);

[[nodiscard]] enum nes_error_t nes_load_rom(struct nes_t *nes,
                                            const struct ines_t ines[static 1]);

void nes_reset(struct nes_t *nes);

void nes_set_pc(struct nes_t *nes, u16 pc);

void nes_set_btn(struct nes_t *nes, enum nes_btn_t btn, bool pressed);

u8 nes_read_ppu(struct nes_t *nes, u16 addr);

const u8 (*nes_get_scanout(const struct nes_t *nes))[NES_SCREEN_WIDTH];

u64 nes_time_cpu(const struct nes_t *nes);

void nes_dispatch_cpu(struct nes_t *nes);

u64 nes_time_pixel(const struct nes_t *nes);

void nes_dispatch_pixel(struct nes_t *nes);

u64 nes_time_dma_cycle(const struct nes_t *nes);

void nes_dispatch_dma_cycle(struct nes_t *nes);

#endif
