#ifndef NESSIE_MAPPER_H
#define NESSIE_MAPPER_H

#include "error.h"
#include "ines.h"
#include "util.h"
#include <stddef.h>
#include <stdlib.h>

struct mapper_ppu_read_t {
    enum {
        MAPPER_PPU_READ_DIRECT,
        MAPPER_PPU_READ_VRAM,
    } kind;
    union {
        u8 direct_value;
        u16 vram_addr;
    };
};

struct mapper_ppu_write_t {
    enum {
        MAPPER_PPU_WRITE_DONE,
        MAPPER_PPU_WRITE_VRAM,
    } kind;
    union {
        struct {
            u8 value;
            u16 addr;
        } vram;
    };
};

struct mapper_vtable_t {
    void (*deinit)(void *ptr);
    u8 (*read)(void *ptr, u16 addr);
    u8 (*peek)(const void *ptr, u16 addr);
    void (*write)(void *ptr, u16 addr, u8 value);
    struct mapper_ppu_read_t (*read_ppu)(void *ptr, uint(14) addr);
    struct mapper_ppu_write_t (*write_ppu)(void *ptr, uint(14) addr, u8 value);
};

struct mapper_t {
    void *ptr;
    const struct mapper_vtable_t *vtable;
};

static inline void mapper_deinit(struct mapper_t mapper)
{
    mapper.vtable->deinit(mapper.ptr);
    free(mapper.ptr);
}

static inline u8 mapper_read(struct mapper_t mapper, u16 addr)
{
    return mapper.vtable->read(mapper.ptr, addr);
}

static inline u8 mapper_peek(struct mapper_t mapper, u16 addr)
{
    return mapper.vtable->peek(mapper.ptr, addr);
}

static inline void mapper_write(struct mapper_t mapper, u16 addr, u8 value)
{
    mapper.vtable->write(mapper.ptr, addr, value);
}

static inline struct mapper_ppu_read_t mapper_read_ppu(struct mapper_t mapper,
                                                       uint(14) addr)
{
    return mapper.vtable->read_ppu(mapper.ptr, addr);
}

static inline struct mapper_ppu_write_t
mapper_write_ppu(struct mapper_t mapper, uint(14) addr, u8 value)
{
    return mapper.vtable->write_ppu(mapper.ptr, addr, value);
}

[[nodiscard]] enum nes_error_t
mapper_from_rom(const struct ines_t ines[static 1],
                struct mapper_t *out_mapper);

#endif
