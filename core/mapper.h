#ifndef NESSIE_MAPPER_H
#define NESSIE_MAPPER_H

#include "error.h"
#include "ines.h"
#include "util.h"
#include <stddef.h>
#include <stdlib.h>

struct mapper_vtable_t {
    void (*deinit)(void *ptr);
    u8 (*read)(void *ptr, u16 addr);
    void (*write)(void *ptr, u16 addr, u8 value);
    u8 (*read_ppu)(void *ptr, u16 addr);
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

static inline void mapper_write(struct mapper_t mapper, u16 addr, u8 value)
{
    mapper.vtable->write(mapper.ptr, addr, value);
}

static inline u8 mapper_read_ppu(struct mapper_t mapper, u16 addr)
{
    return mapper.vtable->read_ppu(mapper.ptr, addr);
}

[[nodiscard]] enum nes_error_t
mapper_from_rom(const struct ines_t ines[static 1],
                struct mapper_t *out_mapper);

#endif
