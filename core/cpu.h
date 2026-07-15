#ifndef NESSIE_CPU_H
#define NESSIE_CPU_H

#include "util.h"
#include <stddef.h>
#include <stdlib.h>

struct memory_vtable_t {
    void (*deinit)(void *ptr);
    u8 (*read)(void *ptr, u16 addr);
    u8 (*peek)(const void *ptr, u16 addr);
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

static inline u8 memory_peek(struct memory_t mem, u16 addr)
{
    return mem.vtable->peek(mem.ptr, addr);
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

static constexpr size_t CPU_INSTR_BYTES_MAX = 3;

enum cpu_op_kind_t : u8 {
    OP_IMPL,
    OP_A,
    OP_ADDR,
    OP_ABS,
    OP_ABS_X,
    OP_ABS_Y,
    OP_IMM,
    OP_IND,
    OP_X_IND,
    OP_IND_Y,
    OP_ZPG,
    OP_ZPG_X,
    OP_ZPG_Y,
};

struct cpu_op_trace_t {
    enum cpu_op_kind_t kind;
    union {
        struct {
            u16 addr;
            u8 addr_value;
        } abs;
        struct {
            u16 addr;
        } addr;
        struct {
            u16 addr;
            u16 addr_xy;
            u8 addr_xy_value;
        } abs_xy;
        struct {
            u8 value;
        } imm;
        struct {
            u16 addr;
            u16 eff_addr;
        } ind;
        struct {
            u8 addr;
            u8 addr_x;
            u16 eff_addr;
            u8 eff_addr_value;
        } x_ind;
        struct {
            u8 addr;
            u16 eff_addr;
            u16 eff_addr_y;
            u8 eff_addr_y_value;
        } ind_y;
        struct {
            u8 addr;
            u8 addr_value;
        } zpg;
        struct {
            u8 addr;
            u8 addr_xy;
            u8 addr_xy_value;
        } zpg_xy;
    };
};

struct cpu_trace_instr_t {
    struct cpu_op_trace_t op;
    const char *mnemonic;
    size_t bytes_cnt;
    u16 bytes[CPU_INSTR_BYTES_MAX];
    bool illegal;
};

struct cpu_trace_t {
    struct cpu_trace_instr_t instr;
    size_t cyc;
    u16 pc;
    u8 s;
    u8 a;
    u8 x;
    u8 y;
    u8 p;
};

struct cpu_t {
    struct cpu_trace_t trace;
    size_t cyc;
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
