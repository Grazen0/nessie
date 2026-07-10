#ifndef NESSIE_SCHEDULER_H
#define NESSIE_SCHEDULER_H

#include "nes.h"
#include "util.h"
#include <assert.h>
#include <stddef.h>

static constexpr size_t SCHED_EVENT_COUNT = 1;
static_assert(SCHED_EVENT_COUNT > 0);

struct sched_t {
    u64 times[SCHED_EVENT_COUNT];
};

struct sched_t *sched_init(struct sched_t *sched);

u64 sched_next_time(const struct sched_t sched[static 1]);

void sched_dispatch(struct sched_t sched[static 1], struct nes_t nes[static 1]);

void sched_dispatch_until(struct sched_t sched[static 1],
                          struct nes_t nes[static 1], u64 until);

#endif
