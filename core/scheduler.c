#include "scheduler.h"
#include "nes.h"
#include "util.h"
#include <assert.h>
#include <stddef.h>

struct sched_t *sched_init(struct sched_t *sched)
{
    if (sched != nullptr) {
        *sched = (struct sched_t){
            .times = {},
        };
    }

    return sched;
}

u64 sched_next_time(const struct sched_t sched[static 1])
{
    u64 min_time = sched->times[0];

    for (size_t i = 1; i < SCHED_EVENT_COUNT; ++i) {
        if (sched->times[i] < min_time)
            min_time = sched->times[i];
    }

    return min_time;
}

void sched_dispatch(struct sched_t sched[static 1], struct nes_t nes[static 1])
{
    static u64 (*const DISPATCHERS[])(struct nes_t[static 1]) = {
        nes_dispatch_cpu,
    };
    static_assert(ARRAY_LEN(DISPATCHERS) == SCHED_EVENT_COUNT);

    size_t next_ev = 0;

    for (size_t i = 1; i < SCHED_EVENT_COUNT; ++i) {
        if (sched->times[i] < sched->times[next_ev])
            next_ev = i;
    }

    u64 elapsed = DISPATCHERS[next_ev](nes);
    sched->times[next_ev] += elapsed;
}

void sched_dispatch_until(struct sched_t sched[static 1],
                          struct nes_t nes[static 1], u64 until)
{
    while (sched_next_time(sched) < until)
        sched_dispatch(sched, nes);
}
