#include "scheduler.h"
#include "nes.h"
#include "util.h"
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

static u64 (*const EVENT_TIMES[])(const struct nes_t *) = {
    nes_time_cpu,
    nes_time_pixel,
    nes_time_dma_cycle,
};

static void (*const EVENT_DISPATCHERS[])(struct nes_t *) = {
    nes_dispatch_cpu,
    nes_dispatch_pixel,
    nes_dispatch_dma_cycle,
};
static_assert(ARRAY_LEN(EVENT_DISPATCHERS) == ARRAY_LEN(EVENT_TIMES));

static constexpr size_t SCHED_EVENT_COUNT = ARRAY_LEN(EVENT_DISPATCHERS);

struct sched_t {
    u64 times[SCHED_EVENT_COUNT];
};

struct sched_t *sched_init(struct sched_t *sched, const struct nes_t *nes)
{
    if (sched != nullptr) {
        *sched = (struct sched_t){};

        for (size_t i = 0; i < SCHED_EVENT_COUNT; ++i)
            sched->times[i] = EVENT_TIMES[i](nes);
    }

    return sched;
}

struct sched_t *sched_create(const struct nes_t *nes)
{
    return sched_init(calloc(1, sizeof(struct sched_t)), nes);
}

void sched_deinit(struct sched_t *sched)
{
    if (sched != nullptr)
        *sched = (struct sched_t){};
}

void sched_destroy(struct sched_t *sched)
{
    sched_deinit(sched);
    free(sched);
}

u64 sched_next_time(const struct sched_t *sched)
{
    u64 min_time = sched->times[0];

    for (size_t i = 1; i < SCHED_EVENT_COUNT; ++i) {
        if (sched->times[i] < min_time)
            min_time = sched->times[i];
    }

    return min_time;
}

void sched_dispatch(struct sched_t *sched, struct nes_t *nes)
{
    size_t next_ev = 0;

    for (size_t i = 1; i < SCHED_EVENT_COUNT; ++i) {
        if (sched->times[i] < sched->times[next_ev])
            next_ev = i;
    }

    EVENT_DISPATCHERS[next_ev](nes);
    sched->times[next_ev] = EVENT_TIMES[next_ev](nes);
}

void sched_dispatch_until(struct sched_t *sched, struct nes_t *nes, u64 until)
{
    while (sched_next_time(sched) < until)
        sched_dispatch(sched, nes);
}
