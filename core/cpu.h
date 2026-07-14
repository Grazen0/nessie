#ifndef NESSIE_CPU_H
#define NESSIE_CPU_H

#include "util.h"
#include <stddef.h>
#include <stdlib.h>

struct memory_vtable_t {
    void (*deinit)(void *ptr);
    u8 (*read)(void *ptr, u16 addr);
    void (*write)(void *ptr, u16 addr, u8 value);
};

struct memory_t {
    void *ptr;
    const struct memory_vtable_t *vtable;
};

static inline u8 memory_read(struct memory_t mem, u16 addr)
{
    return mem.vtable->read(mem.ptr, addr);
}

static inline void memory_write(struct memory_t mem, u16 addr, u8 value)
{
    mem.vtable->write(mem.ptr, addr, value);
}

static inline void memory_deinit(struct memory_t mem)
{
    mem.vtable->deinit(mem.ptr);
    free(mem.ptr);
}

struct cpu_t {
    size_t cycles;
    u16 pc;
    u8 s;
    u8 a;
    u8 x;
    u8 y;
    u8 p;
};

struct cpu_t *cpu_init(struct cpu_t *cpu);

void cpu_step(struct cpu_t cpu[static 1], struct memory_t mem);

void cpu_reset(struct cpu_t cpu[static 1], struct memory_t mem);

bool cpu_request_irq(struct cpu_t cpu[static 1], struct memory_t mem);

void cpu_request_nmi(struct cpu_t cpu[static 1], struct memory_t mem);

#endif
