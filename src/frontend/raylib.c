#include "common.h"
#include "nes.h"
#include "scheduler.h"
#include "util.h"
#include <raylib.h>
#include <raymath.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

// https://www.nesdev.org/wiki/PPU_palettes
static const u8 COLORS[] = {
#embed "../../data/2C02G_U_wiki.pal"
};

static void update_pixels(struct nes_t nes[static 1], Texture tex,
                          Color pixels[static tex.width * tex.height])
{
    for (size_t y = 0; y < NES_SCREEN_HEIGHT; ++y) {
        for (size_t x = 0; x < NES_SCREEN_WIDTH; ++x) {
            size_t col = nes->scanout_buf[y][x];
            const u8 *rgb = &COLORS[3 * col];
            pixels[(y * tex.width) + x] = (Color){rgb[0], rgb[1], rgb[2], 255};
        }
    }
}

Rectangle fit_rect_to_ratio(float cx, float cy, float cw, float ch, float ratio)
{
    double c_ratio = cw / ch;

    if (c_ratio > ratio) {
        float w = ch * ratio;
        return (Rectangle){
            .x = cx + ((cw - w) / 2.0F),
            .y = cy,
            .width = w,
            .height = ch,
        };
    }

    if (c_ratio < ratio) {
        float h = cw / ratio;
        return (Rectangle){
            .x = cx,
            .y = cy + ((ch - h) / 2.0F),
            .width = cw,
            .height = h,
        };
    }

    return (Rectangle){.x = cx, .y = cy, .width = cw, .height = ch};
}

static void draw(struct nes_t nes[static 1], Texture2D tex, Color *pixels)
{
    update_pixels(nes, tex, pixels);
    UpdateTexture(tex, pixels);

    Rectangle source = {0, 0, (float)tex.width, (float)tex.height};
    Rectangle dest =
        fit_rect_to_ratio(0, 0, (float)GetScreenWidth(),
                          (float)GetScreenHeight(), NES_SCREEN_RATIO);

    BeginDrawing();
    ClearBackground(BLACK);
    DrawTexturePro(tex, source, dest, Vector2Zero(), 0, WHITE);
    EndDrawing();
}

static int run(struct nes_t nes[static 1], struct sched_t *sched)
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(WINDOW_INIT_WIDTH, WINDOW_INIT_HEIGHT, "gemu");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);

    Color *pixels =
        MemAlloc(NES_SCREEN_WIDTH * NES_SCREEN_HEIGHT * sizeof(*pixels));
    assert(pixels != nullptr);

    Image image = {
        .data = pixels,
        .width = NES_SCREEN_WIDTH,
        .height = NES_SCREEN_HEIGHT,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
    };

    Texture2D texture = LoadTextureFromImage(image);

    long double start = GetTime();

    while (!WindowShouldClose()) {
        long double frame_start = GetTime();

        nes->btns.start = IsKeyDown(KEY_ENTER);
        nes->btns.select = IsKeyDown(KEY_SPACE);
        nes->btns.up = IsKeyDown(KEY_UP);
        nes->btns.down = IsKeyDown(KEY_DOWN);
        nes->btns.left = IsKeyDown(KEY_LEFT);
        nes->btns.right = IsKeyDown(KEY_RIGHT);
        nes->btns.a = IsKeyDown(KEY_X);
        nes->btns.b = IsKeyDown(KEY_Z);

        long double cur_time = frame_start - start;
        u64 cur_time_clk = (u64)(cur_time * NES_MASTER_CLK_FREQ);

        sched_dispatch_until(sched, nes, cur_time_clk);
        draw(nes, texture, pixels);
    }

    return EXIT_SUCCESS;
}

const struct frontend_t frontend = {
    .name = "raylib",
    .run = run,
};
