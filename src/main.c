#include <stdint.h>
#include <stdio.h>

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

typedef int64_t i64;
typedef int32_t i32;
typedef int16_t i16;
typedef int8_t i8;

enum cpu_flag_t : u8 {
    FLAG_N = 1 << 7, // negative
    PLAG_V = 1 << 6, // overflow
    PLAG_B = 1 << 4, // break
    PLAG_D = 1 << 3, // decimal
    PLAG_I = 1 << 2, // interrupt disable
    PLAG_Z = 1 << 1, // zero
    PLAG_C = 1 << 0, // carry
};

struct cpu_t {
    u16 pc;
    u8 s;
    u8 a;
    u8 x;
    u8 y;
    u8 p;
};

typedef struct {
    u8 (*read)(void *ptr, u16 addr);
    void (*write)(void *ptr, u16 addr, u8 value);
    void (*deinit)(void *ptr);
} MemoryVTable;

typedef struct {
    void *ptr;
    const MemoryVTable *vtable;
} Memory;

static inline u8 memory_read(Memory mem, u16 addr)
{
    return mem.vtable->read(mem.ptr, addr);
}

static inline void memory_write(Memory mem, u16 addr, u8 value)
{
    mem.vtable->write(mem.ptr, addr, value);
}

static inline void memory_deinit(Memory mem)
{
    mem.vtable->deinit(mem.ptr);
}

static u16 memory_read_u16(Memory mem, u16 addr)
{
    u16 lo = memory_read(mem, addr);
    u16 hi = memory_read(mem, addr + 1);
    return (hi << 8) | lo;
}

static u16 cpu_read_pc(struct cpu_t *cpu, Memory mem)
{
    return memory_read(mem, cpu->pc++);
}

static u16 cpu_read_pc_u16(struct cpu_t *cpu, Memory mem)
{
    u16 lo = cpu_read_pc(cpu, mem);
    u16 hi = cpu_read_pc(cpu, mem);

    return (hi << 8) | lo;
}

static void cpu_step(struct cpu_t *cpu, Memory mem)
{
    u8 opcode = cpu_read_pc(cpu, mem);

    u8 aaa = (opcode >> 5) & 0b111;
    u8 bbb = (opcode >> 2) & 0b111;
    u8 cc = opcode & 0b11;
}

int main()
{
    printf("hello\n");
    return 0;
}
