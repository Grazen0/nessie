#ifndef NESSIE_SCHEDULER_H
#define NESSIE_SCHEDULER_H

#include "nes.h"
#include "util.h"
#include <assert.h>
#include <stddef.h>

struct sched_t;

struct sched_t *sched_init(struct sched_t *sched);

struct sched_t *sched_create();

void sched_deinit(struct sched_t *sched);

void sched_destroy(struct sched_t *sched);

u64 sched_next_time(const struct sched_t *sched);

void sched_dispatch(struct sched_t *sched, struct nes_t nes[static 1]);

void sched_dispatch_until(struct sched_t *sched, struct nes_t nes[static 1],
                          u64 until);

#endif
