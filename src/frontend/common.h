#ifndef NESSIE_FRONTEND_COMMON_H
#define NESSIE_FRONTEND_COMMON_H

#include "nes.h"
#include "scheduler.h"

static constexpr int WINDOW_INIT_WIDTH = 3 * NES_SCREEN_WIDTH;
static constexpr int WINDOW_INIT_HEIGHT = 3 * NES_SCREEN_HEIGHT;

struct frontend_t {
    const char *name;
    int (*run)(struct nes_t[static 1], struct sched_t *);
};

extern const struct frontend_t frontend;

#endif
