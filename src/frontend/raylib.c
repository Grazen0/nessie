#include "common.h"
#include "nes.h"
#include "scheduler.h"
#include <raylib.h>
#include <stdlib.h>

static int run(struct nes_t nes[static 1], struct sched_t sched[static 1])
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(100, 100, "gemu");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);

    long double start = GetTime();

    while (!WindowShouldClose()) {
        long double frame_start = GetTime();

        long double cur_time = frame_start - start;
        u64 cur_time_clk = (u64)(cur_time * NES_CLK_FREQ_HZ);

        sched_dispatch_until(sched, nes, cur_time_clk);

        BeginDrawing();
        EndDrawing();
    }

    return EXIT_SUCCESS;
}

const struct frontend_t frontend = {
    .name = "raylib",
    .run = run,
};
