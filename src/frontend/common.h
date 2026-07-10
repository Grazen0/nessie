#ifndef NESSIE_FRONTEND_COMMON_H
#define NESSIE_FRONTEND_COMMON_H

#include "nes.h"
#include "scheduler.h"

struct frontend_t {
    const char *name;
    int (*run)(struct nes_t[static 1], struct sched_t[static 1]);
};

extern const struct frontend_t frontend;

#endif
