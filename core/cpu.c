#include "cpu.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>

static constexpr u16 NMI_LOC = 0xFFFA;
static constexpr u16 RES_LOC = 0xFFFC;
static constexpr u16 IRQ_LOC = 0xFFFE;

enum cpu_flag_t : u8 {
    FLAG_N = 1 << 7, // negative
    FLAG_V = 1 << 6, // overflow
    FLAG_B = 1 << 4, // break
    FLAG_D = 1 << 3, // decimal
    FLAG_I = 1 << 2, // interrupt disable
    FLAG_Z = 1 << 1, // zero
    FLAG_C = 1 << 0, // carry
};

static u16 memory_read_u16(struct memory_t mem, u16 addr)
{
    u16 lo = memory_read(mem, addr);
    u16 hi = memory_read(mem, addr + 1);
    return (hi << 8) | lo;
}

static void memory_write_u16(struct memory_t mem, u16 addr, u16 value)
{
    memory_write(mem, addr, value & 0xFF);
    memory_write(mem, addr, (value >> 8) & 0xFF);
}

struct cpu_t cpu_init()
{
    return (struct cpu_t){
        .cycles = 0,
        .pc = 0,
        .s = 0,
        .a = 0,
        .x = 0,
        .y = 0,
        .p = 0,
        .irq_disable = true,
    };
}

static inline u16 cpu_sp(const struct cpu_t *cpu)
{
    return (u16)cpu->s | 0x100;
}

static u16 cpu_read_pc(struct cpu_t *cpu, struct memory_t mem)
{
    return memory_read(mem, cpu->pc++);
}

static u16 cpu_read_pc_u16(struct cpu_t *cpu, struct memory_t mem)
{
    u16 lo = cpu_read_pc(cpu, mem);
    u16 hi = cpu_read_pc(cpu, mem);
    return (hi << 8) | lo;
}

void cpu_step(struct cpu_t cpu[static 1], struct memory_t mem)
{
    u8 opcode = cpu_read_pc(cpu, mem);

    switch (opcode) {
        default:
            fprintf(stderr, "Invalid opcode ($%02X)\n", opcode);
            exit(1);
    }
}

void cpu_reset(struct cpu_t cpu[static 1], struct memory_t mem)
{
    cpu->pc = memory_read_u16(mem, RES_LOC);
    cpu->irq_disable = true;
    cpu->cycles += 7;
}
