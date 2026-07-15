#include "cpu.h"
#include "util.h"
#include <assert.h>
#include <stdckdint.h>

static constexpr u16 NMI_LOC = 0xFFFA;
static constexpr u16 RES_LOC = 0xFFFC;
static constexpr u16 IRQ_LOC = 0xFFFE;

enum cpu_flag_t : u8 {
    FLAG_N = 1 << 7, // negative
    FLAG_V = 1 << 6, // overflow
    FLAG_B = 1 << 4, // break
    FLAG_5 = 1 << 5, // unused
    FLAG_D = 1 << 3, // decimal
    FLAG_I = 1 << 2, // interrupt disable
    FLAG_Z = 1 << 1, // zero
    FLAG_C = 1 << 0, // carry
};

static u8 cpu_read_mem(struct cpu_t cpu[static 1], struct memory_t mem,
                       u16 addr)
{
    ++cpu->cyc;
    return memory_read(mem, addr);
}

static void cpu_write_mem(struct cpu_t cpu[static 1], struct memory_t mem,
                          u16 addr, u8 value)
{
    ++cpu->cyc;
    memory_write(mem, addr, value);
}

static u16 cpu_read_mem_u16(struct cpu_t cpu[static 1], struct memory_t mem,
                            u16 addr)
{
    u16 lo = cpu_read_mem(cpu, mem, addr);
    u16 hi = cpu_read_mem(cpu, mem, addr + 1);
    return (hi << 8) | lo;
}

static u16 cpu_read_mem_u16_wrap_page(struct cpu_t cpu[static 1],
                                      struct memory_t mem, u16 addr)
{
    u16 lo = cpu_read_mem(cpu, mem, addr);
    u16 hi = cpu_read_mem(cpu, mem, (addr & 0xFF00) | ((addr + 1) & 0x00FF));
    return (hi << 8) | lo;
}

static u16 cpu_read_zpg_u16(struct cpu_t cpu[static 1], struct memory_t mem,
                            u8 addr)
{
    return cpu_read_mem_u16_wrap_page(cpu, mem, addr);
}

struct cpu_t *cpu_init(struct cpu_t *cpu)
{
    *cpu = (struct cpu_t){};
    cpu->p = FLAG_5;
    return cpu;
}

static inline u16 cpu_sp(const struct cpu_t cpu[static 1])
{
    return (u16)cpu->s | 0x100;
}

static void cpu_st_push(struct cpu_t cpu[static 1], struct memory_t mem,
                        u8 value)
{
    cpu_write_mem(cpu, mem, cpu_sp(cpu), value);
    --cpu->s;
}

static void cpu_st_push_u16(struct cpu_t cpu[static 1], struct memory_t mem,
                            u16 value)
{
    cpu_st_push(cpu, mem, (value >> 8) & 0xFF);
    cpu_st_push(cpu, mem, value & 0xFF);
}

static u8 cpu_st_pull(struct cpu_t cpu[static 1], struct memory_t mem)
{
    ++cpu->s;
    ++cpu->cyc;
    return cpu_read_mem(cpu, mem, cpu_sp(cpu));
}

static u16 cpu_st_pull_u16(struct cpu_t cpu[static 1], struct memory_t mem)
{
    u16 lo = cpu_st_pull(cpu, mem);
    u16 hi = cpu_st_pull(cpu, mem);
    --cpu->cyc;
    return lo | (hi << 8);
}

static u16 cpu_read_pc(struct cpu_t *cpu, struct memory_t mem)
{
    return cpu_read_mem(cpu, mem, cpu->pc++);
}

static u16 cpu_read_pc_u16(struct cpu_t *cpu, struct memory_t mem)
{
    u16 lo = cpu_read_pc(cpu, mem);
    u16 hi = cpu_read_pc(cpu, mem);
    return (hi << 8) | lo;
}

enum opkind_t : u8 {
    OP_IMPL,
    OP_A,
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

struct operand_t {
    enum opkind_t kind;
    u16 addr; // effective address
};

static struct operand_t cpu_compute_operand(struct cpu_t cpu[static 1],
                                            struct memory_t mem,
                                            enum opkind_t kind, bool is_write)
{
    struct operand_t op = {
        .kind = kind,
        .addr = 0,
    };

    switch (kind) {
        case OP_IMPL:
        case OP_A:
        case OP_IMM: // 0 cycles
            break;
        case OP_ZPG: // 1 cycle
            op.addr = cpu_read_pc(cpu, mem);
            break;
        case OP_ZPG_X: // 2 cycles
            op.addr = (cpu_read_pc(cpu, mem) + cpu->x) & 0xFF;
            ++cpu->cyc;
            break;
        case OP_ZPG_Y: // 2 cycles
            op.addr = (cpu_read_pc(cpu, mem) + cpu->y) & 0xFF;
            ++cpu->cyc;
            break;
        case OP_ABS: // 2 cycles
            op.addr = cpu_read_pc_u16(cpu, mem);
            break;
        case OP_ABS_X: { // 2+ cycles
            u16 base = cpu_read_pc_u16(cpu, mem);
            if (is_write || (base & 0xFF) + cpu->x >= 0x100)
                ++cpu->cyc;

            op.addr = base + cpu->x;
            break;
        }
        case OP_ABS_Y: { // 2+ cycles
            u16 base = cpu_read_pc_u16(cpu, mem);
            if (is_write || (base & 0xFF) + cpu->y >= 0x100)
                ++cpu->cyc;

            op.addr = base + cpu->y;
            break;
        }
        case OP_IND: { // 4 cycles
            u16 ind_addr = cpu_read_pc_u16(cpu, mem);
            op.addr = cpu_read_mem_u16_wrap_page(cpu, mem, ind_addr);
            break;
        }
        case OP_X_IND: { // 4 cycles
            u8 ind_addr = cpu_read_pc(cpu, mem) + cpu->x;
            ++cpu->cyc;

            op.addr = cpu_read_zpg_u16(cpu, mem, ind_addr);
            break;
        }
        case OP_IND_Y: { // 3+ cycles
            u8 ind_addr = cpu_read_pc(cpu, mem);
            u16 addr = cpu_read_zpg_u16(cpu, mem, ind_addr);

            if (is_write || (addr & 0xFF) + cpu->y >= 0x100)
                ++cpu->cyc;

            op.addr = addr + cpu->y;
            break;
        }
    }

    return op;
}

static u8 cpu_read_operand(struct cpu_t cpu[static 1], struct memory_t mem,
                           struct operand_t op)
{
    assert(op.kind != OP_IMPL && "cannot read implied operand");

    if (op.kind == OP_A)
        return cpu->a;

    if (op.kind == OP_IMM)
        return cpu_read_pc(cpu, mem);

    return cpu_read_mem(cpu, mem, op.addr);
}

static void cpu_write_operand(struct cpu_t cpu[static 1], struct memory_t mem,
                              struct operand_t op, u8 value)
{
    assert(op.kind != OP_IMM && "cannot write to immediate operand");
    assert(op.kind != OP_IMPL && "cannot write to implied operand");

    if (op.kind == OP_A) {
        cpu->a = value;
        return;
    }

    cpu_write_mem(cpu, mem, op.addr, value);
}

static void cpu_instr_ora(struct cpu_t cpu[static 1], struct memory_t mem,
                          enum opkind_t op_kind)
{
    struct operand_t op = cpu_compute_operand(cpu, mem, op_kind, false);
    cpu->a |= cpu_read_operand(cpu, mem, op);

    set_bits(&cpu->p, FLAG_N, (cpu->a & 0x80) != 0);
    set_bits(&cpu->p, FLAG_Z, cpu->a == 0);
}

static void cpu_instr_and(struct cpu_t cpu[static 1], struct memory_t mem,
                          enum opkind_t op_kind)
{
    struct operand_t op = cpu_compute_operand(cpu, mem, op_kind, false);
    cpu->a &= cpu_read_operand(cpu, mem, op);

    set_bits(&cpu->p, FLAG_N, (cpu->a & 0x80) != 0);
    set_bits(&cpu->p, FLAG_Z, cpu->a == 0);
}

static void cpu_instr_eor(struct cpu_t cpu[static 1], struct memory_t mem,
                          enum opkind_t op_kind)
{
    struct operand_t op = cpu_compute_operand(cpu, mem, op_kind, false);
    cpu->a ^= cpu_read_operand(cpu, mem, op);

    set_bits(&cpu->p, FLAG_N, (cpu->a & 0x80) != 0);
    set_bits(&cpu->p, FLAG_Z, cpu->a == 0);
}

static void cpu_instr_adc(struct cpu_t cpu[static 1], struct memory_t mem,
                          enum opkind_t op_kind)
{
    struct operand_t op = cpu_compute_operand(cpu, mem, op_kind, false);
    u8 rhs = cpu_read_operand(cpu, mem, op);

    i8 a_signed = (i8)cpu->a;

    bool carry = ckd_add(&cpu->a, cpu->a, rhs);
    bool overflow = ckd_add(&a_signed, a_signed, (i8)rhs);

    if ((cpu->p & FLAG_C) != 0) {
        carry = ckd_add(&cpu->a, cpu->a, 1) || carry;
        overflow = ckd_add(&a_signed, a_signed, 1) || overflow;
    }

    set_bits(&cpu->p, FLAG_N, (cpu->a & 0x80) != 0);
    set_bits(&cpu->p, FLAG_Z, cpu->a == 0);
    set_bits(&cpu->p, FLAG_C, carry);
    set_bits(&cpu->p, FLAG_V, overflow);
}

static void cpu_instr_sta(struct cpu_t cpu[static 1], struct memory_t mem,
                          enum opkind_t op_kind)
{
    struct operand_t op = cpu_compute_operand(cpu, mem, op_kind, true);
    cpu_write_operand(cpu, mem, op, cpu->a);
}

static void cpu_instr_lda(struct cpu_t cpu[static 1], struct memory_t mem,
                          enum opkind_t op_kind)
{
    struct operand_t op = cpu_compute_operand(cpu, mem, op_kind, false);
    cpu->a = cpu_read_operand(cpu, mem, op);

    set_bits(&cpu->p, FLAG_N, (cpu->a & 0x80) != 0);
    set_bits(&cpu->p, FLAG_Z, cpu->a == 0);
}

static void cpu_instr_cmp(struct cpu_t cpu[static 1], struct memory_t mem,
                          enum opkind_t op_kind)
{
    struct operand_t op = cpu_compute_operand(cpu, mem, op_kind, false);
    u8 rhs = cpu_read_operand(cpu, mem, op);

    u8 a_cpy = cpu->a;
    bool borrow = ckd_sub(&a_cpy, a_cpy, rhs);

    set_bits(&cpu->p, FLAG_N, (a_cpy & 0x80) != 0);
    set_bits(&cpu->p, FLAG_Z, a_cpy == 0);
    set_bits(&cpu->p, FLAG_C, !borrow);
}

static void cpu_instr_cpx(struct cpu_t cpu[static 1], struct memory_t mem,
                          enum opkind_t op_kind)
{
    struct operand_t op = cpu_compute_operand(cpu, mem, op_kind, false);
    u8 rhs = cpu_read_operand(cpu, mem, op);

    u8 x_cpy = cpu->x;
    bool borrow = ckd_sub(&x_cpy, x_cpy, rhs);

    set_bits(&cpu->p, FLAG_N, (x_cpy & 0x80) != 0);
    set_bits(&cpu->p, FLAG_Z, x_cpy == 0);
    set_bits(&cpu->p, FLAG_C, !borrow);
}

static void cpu_instr_cpy(struct cpu_t cpu[static 1], struct memory_t mem,
                          enum opkind_t op_kind)
{
    struct operand_t op = cpu_compute_operand(cpu, mem, op_kind, false);
    u8 rhs = cpu_read_operand(cpu, mem, op);

    u8 y_cpy = cpu->y;
    bool borrow = ckd_sub(&y_cpy, y_cpy, rhs);

    set_bits(&cpu->p, FLAG_N, (y_cpy & 0x80) != 0);
    set_bits(&cpu->p, FLAG_Z, y_cpy == 0);
    set_bits(&cpu->p, FLAG_C, !borrow);
}

static void cpu_instr_sbc(struct cpu_t cpu[static 1], struct memory_t mem,
                          enum opkind_t op_kind)
{
    struct operand_t op = cpu_compute_operand(cpu, mem, op_kind, false);
    u8 rhs = cpu_read_operand(cpu, mem, op);

    i8 a_signed = (i8)cpu->a;

    bool borrow = ckd_sub(&cpu->a, cpu->a, rhs);
    bool overflow = ckd_sub(&a_signed, a_signed, (i8)rhs);

    if ((cpu->p & FLAG_C) == 0) {
        borrow = ckd_sub(&cpu->a, cpu->a, 1) || borrow;
        overflow = ckd_sub(&a_signed, a_signed, 1) || overflow;
    }

    set_bits(&cpu->p, FLAG_N, (cpu->a & 0x80) != 0);
    set_bits(&cpu->p, FLAG_Z, cpu->a == 0);
    set_bits(&cpu->p, FLAG_C, !borrow);
    set_bits(&cpu->p, FLAG_V, overflow);
}

static void cpu_instr_asl(struct cpu_t cpu[static 1], struct memory_t mem,
                          enum opkind_t op_kind)
{
    struct operand_t op = cpu_compute_operand(cpu, mem, op_kind, true);
    u8 val = cpu_read_operand(cpu, mem, op);

    u8 val_new = val << 1;
    ++cpu->cyc;

    cpu_write_operand(cpu, mem, op, val_new);

    set_bits(&cpu->p, FLAG_N, (val_new & 0x80) != 0);
    set_bits(&cpu->p, FLAG_Z, val_new == 0);
    set_bits(&cpu->p, FLAG_C, (val & 0x80) != 0);
}

static void cpu_instr_lsr(struct cpu_t cpu[static 1], struct memory_t mem,
                          enum opkind_t op_kind)
{
    struct operand_t op = cpu_compute_operand(cpu, mem, op_kind, true);
    u8 val = cpu_read_operand(cpu, mem, op);

    u8 val_new = val >> 1;
    ++cpu->cyc;

    cpu_write_operand(cpu, mem, op, val_new);

    set_bits(&cpu->p, FLAG_N, false);
    set_bits(&cpu->p, FLAG_Z, val_new == 0);
    set_bits(&cpu->p, FLAG_C, (val & 1) != 0);
}

static void cpu_instr_rol(struct cpu_t cpu[static 1], struct memory_t mem,
                          enum opkind_t op_kind)
{
    struct operand_t op = cpu_compute_operand(cpu, mem, op_kind, true);
    u8 val = cpu_read_operand(cpu, mem, op);

    u8 in_bit = (cpu->p & FLAG_C) != 0 ? 1 : 0;
    u8 val_new = (val << 1) | in_bit;
    ++cpu->cyc;

    cpu_write_operand(cpu, mem, op, val_new);

    set_bits(&cpu->p, FLAG_N, (val_new & 0x80) != 0);
    set_bits(&cpu->p, FLAG_Z, val_new == 0);
    set_bits(&cpu->p, FLAG_C, (val & 0x80) != 0);
}

static void cpu_instr_ror(struct cpu_t cpu[static 1], struct memory_t mem,
                          enum opkind_t op_kind)
{
    struct operand_t op = cpu_compute_operand(cpu, mem, op_kind, true);
    u8 val = cpu_read_operand(cpu, mem, op);

    u8 in_bit = (cpu->p & FLAG_C) != 0 ? 1 : 0;
    u8 val_new = (val >> 1) | (in_bit << 7);
    ++cpu->cyc;

    cpu_write_operand(cpu, mem, op, val_new);

    set_bits(&cpu->p, FLAG_N, (val_new & 0x80) != 0);
    set_bits(&cpu->p, FLAG_Z, val_new == 0);
    set_bits(&cpu->p, FLAG_C, (val & 1) != 0);
}

static void cpu_instr_stx(struct cpu_t cpu[static 1], struct memory_t mem,
                          enum opkind_t op_kind)
{
    struct operand_t op = cpu_compute_operand(cpu, mem, op_kind, true);
    cpu_write_operand(cpu, mem, op, cpu->x);
}

static void cpu_instr_sty(struct cpu_t cpu[static 1], struct memory_t mem,
                          enum opkind_t op_kind)
{
    struct operand_t op = cpu_compute_operand(cpu, mem, op_kind, true);
    cpu_write_operand(cpu, mem, op, cpu->y);
}

static void cpu_instr_ldx(struct cpu_t cpu[static 1], struct memory_t mem,
                          enum opkind_t op_kind)
{
    struct operand_t op = cpu_compute_operand(cpu, mem, op_kind, false);
    cpu->x = cpu_read_operand(cpu, mem, op);

    set_bits(&cpu->p, FLAG_N, (cpu->x & 0x80) != 0);
    set_bits(&cpu->p, FLAG_Z, cpu->x == 0);
}

static void cpu_instr_ldy(struct cpu_t cpu[static 1], struct memory_t mem,
                          enum opkind_t op_kind)
{
    struct operand_t op = cpu_compute_operand(cpu, mem, op_kind, false);
    cpu->y = cpu_read_operand(cpu, mem, op);

    set_bits(&cpu->p, FLAG_N, (cpu->y & 0x80) != 0);
    set_bits(&cpu->p, FLAG_Z, cpu->y == 0);
}

static void cpu_instr_lax(struct cpu_t cpu[static 1], struct memory_t mem,
                          enum opkind_t op_kind)
{
    struct operand_t op = cpu_compute_operand(cpu, mem, op_kind, false);
    cpu->a = cpu_read_operand(cpu, mem, op);
    cpu->x = cpu->a;

    set_bits(&cpu->p, FLAG_N, (cpu->a & 0x80) != 0);
    set_bits(&cpu->p, FLAG_Z, cpu->a == 0);
}

static void cpu_instr_sax(struct cpu_t cpu[static 1], struct memory_t mem,
                          enum opkind_t op_kind)
{
    struct operand_t op = cpu_compute_operand(cpu, mem, op_kind, true);
    cpu_write_operand(cpu, mem, op, cpu->a & cpu->x);
}

static void cpu_instr_dec(struct cpu_t cpu[static 1], struct memory_t mem,
                          enum opkind_t op_kind)
{
    struct operand_t op = cpu_compute_operand(cpu, mem, op_kind, true);
    u8 val = cpu_read_operand(cpu, mem, op);

    --val;
    ++cpu->cyc;

    cpu_write_operand(cpu, mem, op, val);

    set_bits(&cpu->p, FLAG_N, (val & 0x80) != 0);
    set_bits(&cpu->p, FLAG_Z, val == 0);
}

static void cpu_instr_inc(struct cpu_t cpu[static 1], struct memory_t mem,
                          enum opkind_t op_kind)
{
    struct operand_t op = cpu_compute_operand(cpu, mem, op_kind, true);
    u8 val = cpu_read_operand(cpu, mem, op);

    ++val;
    ++cpu->cyc;

    cpu_write_operand(cpu, mem, op, val);

    set_bits(&cpu->p, FLAG_N, (val & 0x80) != 0);
    set_bits(&cpu->p, FLAG_Z, val == 0);
}

static void cpu_instr_jmp(struct cpu_t cpu[static 1], struct memory_t mem,
                          enum opkind_t op_kind)
{
    struct operand_t op = cpu_compute_operand(cpu, mem, op_kind, false);
    cpu->pc = op.addr;
}

static void cpu_instr_bit(struct cpu_t cpu[static 1], struct memory_t mem,
                          enum opkind_t op_kind)
{
    struct operand_t op = cpu_compute_operand(cpu, mem, op_kind, false);
    u8 val = cpu_read_operand(cpu, mem, op);

    set_bits(&cpu->p, FLAG_V, (val & FLAG_V) != 0);
    set_bits(&cpu->p, FLAG_N, (val & FLAG_N) != 0);
    set_bits(&cpu->p, FLAG_Z, (val & cpu->a) == 0);
}

static void cpu_instr_bxx(struct cpu_t cpu[static 1], struct memory_t mem,
                          u8 flag, bool exp_val)
{
    i8 offset = (i8)cpu_read_pc(cpu, mem);

    bool flag_val = (cpu->p & flag) != 0;

    if (flag_val == exp_val) {
        ++cpu->cyc;

        if ((cpu->pc & 0xFF00) != ((cpu->pc + offset) & 0xFF00))
            ++cpu->cyc;

        cpu->pc += offset;
    }
}

static void cpu_instr_nop(struct cpu_t cpu[static 1], struct memory_t mem,
                          enum opkind_t op_kind)
{
    struct operand_t op = cpu_compute_operand(cpu, mem, op_kind, false);
    if (op_kind != OP_IMPL)
        cpu_read_operand(cpu, mem, op);

    ++cpu->cyc;
}

static void cpu_instr_dcp(struct cpu_t cpu[static 1], struct memory_t mem,
                          enum opkind_t op_kind)
{
    struct operand_t op = cpu_compute_operand(cpu, mem, op_kind, true);
    u8 rhs = cpu_read_operand(cpu, mem, op);
    --rhs;
    ++cpu->cyc;
    cpu_write_operand(cpu, mem, op, rhs);

    u8 a_cpy = cpu->a;
    bool borrow = ckd_sub(&a_cpy, a_cpy, rhs);

    set_bits(&cpu->p, FLAG_N, (a_cpy & 0x80) != 0);
    set_bits(&cpu->p, FLAG_Z, a_cpy == 0);
    set_bits(&cpu->p, FLAG_C, !borrow);
}

static void cpu_instr_isc(struct cpu_t cpu[static 1], struct memory_t mem,
                          enum opkind_t op_kind)
{
    struct operand_t op = cpu_compute_operand(cpu, mem, op_kind, true);
    u8 rhs = cpu_read_operand(cpu, mem, op);
    ++rhs;
    ++cpu->cyc;
    cpu_write_operand(cpu, mem, op, rhs);

    i8 a_signed = (i8)cpu->a;

    bool borrow = ckd_sub(&cpu->a, cpu->a, rhs);
    bool overflow = ckd_sub(&a_signed, a_signed, (i8)rhs);

    if ((cpu->p & FLAG_C) == 0) {
        borrow = ckd_sub(&cpu->a, cpu->a, 1) || borrow;
        overflow = ckd_sub(&a_signed, a_signed, 1) || overflow;
    }

    set_bits(&cpu->p, FLAG_N, (cpu->a & 0x80) != 0);
    set_bits(&cpu->p, FLAG_Z, cpu->a == 0);
    set_bits(&cpu->p, FLAG_C, !borrow);
    set_bits(&cpu->p, FLAG_V, overflow);
}

static void cpu_instr_rla(struct cpu_t cpu[static 1], struct memory_t mem,
                          enum opkind_t op_kind)
{
    struct operand_t op = cpu_compute_operand(cpu, mem, op_kind, true);
    u8 val = cpu_read_operand(cpu, mem, op);

    u8 in_bit = (cpu->p & FLAG_C) != 0 ? 1 : 0;
    u8 val_new = (val << 1) | in_bit;
    ++cpu->cyc;
    cpu_write_operand(cpu, mem, op, val_new);

    cpu->a &= val_new;

    set_bits(&cpu->p, FLAG_N, (cpu->a & 0x80) != 0);
    set_bits(&cpu->p, FLAG_Z, cpu->a == 0);
    set_bits(&cpu->p, FLAG_C, (val & 0x80) != 0);
}

static void cpu_instr_slo(struct cpu_t cpu[static 1], struct memory_t mem,
                          enum opkind_t op_kind)
{
    struct operand_t op = cpu_compute_operand(cpu, mem, op_kind, true);
    u8 val = cpu_read_operand(cpu, mem, op);

    u8 val_new = val << 1;
    ++cpu->cyc;

    cpu_write_operand(cpu, mem, op, val_new);
    cpu->a |= val_new;

    set_bits(&cpu->p, FLAG_N, (cpu->a & 0x80) != 0);
    set_bits(&cpu->p, FLAG_Z, cpu->a == 0);
    set_bits(&cpu->p, FLAG_C, (val & 0x80) != 0);
}

static void cpu_instr_sre(struct cpu_t cpu[static 1], struct memory_t mem,
                          enum opkind_t op_kind)
{
    struct operand_t op = cpu_compute_operand(cpu, mem, op_kind, true);
    u8 val = cpu_read_operand(cpu, mem, op);

    u8 val_new = val >> 1;
    ++cpu->cyc;
    cpu_write_operand(cpu, mem, op, val_new);
    cpu->a ^= val_new;

    set_bits(&cpu->p, FLAG_N, (cpu->a & 0x80) != 0);
    set_bits(&cpu->p, FLAG_Z, cpu->a == 0);
    set_bits(&cpu->p, FLAG_C, (val & 1) != 0);
}

static void cpu_instr_rra(struct cpu_t cpu[static 1], struct memory_t mem,
                          enum opkind_t op_kind)
{
    struct operand_t op = cpu_compute_operand(cpu, mem, op_kind, true);
    u8 val = cpu_read_operand(cpu, mem, op);

    u8 in_bit = (cpu->p & FLAG_C) != 0 ? 1 : 0;
    u8 val_new = (val >> 1) | (in_bit << 7);
    ++cpu->cyc;
    cpu_write_operand(cpu, mem, op, val_new);

    i8 a_signed = (i8)cpu->a;

    bool carry = ckd_add(&cpu->a, cpu->a, val_new);
    bool overflow = ckd_add(&a_signed, a_signed, (i8)val_new);

    if ((val & 1) != 0) { // carry would effectively become this
        carry = ckd_add(&cpu->a, cpu->a, 1) || carry;
        overflow = ckd_add(&a_signed, a_signed, 1) || overflow;
    }

    set_bits(&cpu->p, FLAG_N, (cpu->a & 0x80) != 0);
    set_bits(&cpu->p, FLAG_Z, cpu->a == 0);
    set_bits(&cpu->p, FLAG_C, carry);
    set_bits(&cpu->p, FLAG_V, overflow);
}

void cpu_step(struct cpu_t cpu[static 1], struct memory_t mem)
{
    u16 pc_start = cpu->pc;
    u8 opcode = cpu_read_pc(cpu, mem);

    switch (opcode) {
        case 0x18: // clc
            set_bits(&cpu->p, FLAG_C, false);
            ++cpu->cyc;
            break;
        case 0x38: // sec
            set_bits(&cpu->p, FLAG_C, true);
            ++cpu->cyc;
            break;
        case 0x58: // cli
            set_bits(&cpu->p, FLAG_I, false);
            ++cpu->cyc;
            break;
        case 0x78: // sei
            set_bits(&cpu->p, FLAG_I, true);
            ++cpu->cyc;
            break;
        case 0xB8: // clv
            set_bits(&cpu->p, FLAG_V, false);
            ++cpu->cyc;
            break;
        case 0xD8: // cld
            set_bits(&cpu->p, FLAG_D, false);
            ++cpu->cyc;
            break;
        case 0xF8: // sed
            set_bits(&cpu->p, FLAG_D, true);
            ++cpu->cyc;
            break;

        case 0x98: // tya
            cpu->a = cpu->y;
            set_bits(&cpu->p, FLAG_N, (cpu->a & 0x80) != 0);
            set_bits(&cpu->p, FLAG_Z, cpu->a == 0);
            ++cpu->cyc;
            break;
        case 0xA8: // tay
            cpu->y = cpu->a;
            set_bits(&cpu->p, FLAG_N, (cpu->y & 0x80) != 0);
            set_bits(&cpu->p, FLAG_Z, cpu->y == 0);
            ++cpu->cyc;
            break;

        case 0x8A: // txa
            cpu->a = cpu->x;
            set_bits(&cpu->p, FLAG_N, (cpu->a & 0x80) != 0);
            set_bits(&cpu->p, FLAG_Z, cpu->a == 0);
            ++cpu->cyc;
            break;
        case 0x9A: // txs
            cpu->s = cpu->x;
            ++cpu->cyc;
            break;
        case 0xAA: // tax
            cpu->x = cpu->a;
            set_bits(&cpu->p, FLAG_N, (cpu->x & 0x80) != 0);
            set_bits(&cpu->p, FLAG_Z, cpu->x == 0);
            ++cpu->cyc;
            break;
        case 0xBA: // tsx
            cpu->x = cpu->s;
            set_bits(&cpu->p, FLAG_N, (cpu->x & 0x80) != 0);
            set_bits(&cpu->p, FLAG_Z, cpu->x == 0);
            ++cpu->cyc;
            break;

        case 0x08: // php
            // p must be pushed with break flag and bit 5 set
            cpu_st_push(cpu, mem, cpu->p | FLAG_B);
            ++cpu->cyc;
            break;
        case 0x28: // plp
            cpu->p = (cpu_st_pull(cpu, mem) & ~FLAG_B) | FLAG_5;
            ++cpu->cyc;
            break;
        case 0x48: // pha
            cpu_st_push(cpu, mem, cpu->a);
            ++cpu->cyc;
            break;
        case 0x68: // pla
            cpu->a = cpu_st_pull(cpu, mem);
            set_bits(&cpu->p, FLAG_N, (cpu->a & 0x80) != 0);
            set_bits(&cpu->p, FLAG_Z, cpu->a == 0);
            ++cpu->cyc;
            break;

        case 0xE8: // inx
            ++cpu->x;
            ++cpu->cyc;
            set_bits(&cpu->p, FLAG_N, (cpu->x & 0x80) != 0);
            set_bits(&cpu->p, FLAG_Z, cpu->x == 0);
            break;
        case 0xCA: // dex
            --cpu->x;
            ++cpu->cyc;
            set_bits(&cpu->p, FLAG_N, (cpu->x & 0x80) != 0);
            set_bits(&cpu->p, FLAG_Z, cpu->x == 0);
            break;
        case 0xC8: // iny
            ++cpu->y;
            ++cpu->cyc;
            set_bits(&cpu->p, FLAG_N, (cpu->y & 0x80) != 0);
            set_bits(&cpu->p, FLAG_Z, cpu->y == 0);
            break;
        case 0x88: // dey
            --cpu->y;
            ++cpu->cyc;
            set_bits(&cpu->p, FLAG_N, (cpu->y & 0x80) != 0);
            set_bits(&cpu->p, FLAG_Z, cpu->y == 0);
            break;

        case 0x20: // jsr
            u16 addr = cpu_read_pc_u16(cpu, mem);
            cpu_st_push_u16(cpu, mem, cpu->pc - 1);
            cpu->pc = addr;
            ++cpu->cyc;
            break;

        case 0x00: // brk
            cpu_st_push_u16(cpu, mem, cpu->pc);
            cpu_st_push(cpu, mem, cpu->p | FLAG_B);

            cpu->pc = cpu_read_mem_u16(cpu, mem, IRQ_LOC);
            set_bits(&cpu->p, FLAG_I, true);

            ++cpu->cyc;
            break;

        case 0x60: // rts
            cpu->pc = cpu_st_pull_u16(cpu, mem) + 1;
            cpu->cyc += 2;
            break;

        case 0x40: // rti
            cpu->p = (cpu_st_pull(cpu, mem) & ~FLAG_B) | FLAG_5;
            cpu->pc = cpu_st_pull_u16(cpu, mem);
            break;

            // clang-format off
        case 0xEA: cpu_instr_nop(cpu, mem, OP_IMPL); break;

        case 0x24: cpu_instr_bit(cpu, mem, OP_ZPG); break;
        case 0x2C: cpu_instr_bit(cpu, mem, OP_ABS); break;

        case 0x4C: cpu_instr_jmp(cpu, mem, OP_ABS); break;
        case 0x6C: cpu_instr_jmp(cpu, mem, OP_IND); break;

        case 0x10: cpu_instr_bxx(cpu, mem, FLAG_N, false); break; // bpl
        case 0x30: cpu_instr_bxx(cpu, mem, FLAG_N, true); break; // bmi
        case 0x50: cpu_instr_bxx(cpu, mem, FLAG_V, false); break; // bvc
        case 0x70: cpu_instr_bxx(cpu, mem, FLAG_V, true); break; // bvs
        case 0x90: cpu_instr_bxx(cpu, mem, FLAG_C, false); break; // bcc
        case 0xB0: cpu_instr_bxx(cpu, mem, FLAG_C, true); break; // bcs
        case 0xD0: cpu_instr_bxx(cpu, mem, FLAG_Z, false); break; // bne
        case 0xF0: cpu_instr_bxx(cpu, mem, FLAG_Z, true); break; // beq

        case 0xE0: cpu_instr_cpx(cpu, mem, OP_IMM); break;
        case 0xE4: cpu_instr_cpx(cpu, mem, OP_ZPG); break;
        case 0xEC: cpu_instr_cpx(cpu, mem, OP_ABS); break;

        case 0xC0: cpu_instr_cpy(cpu, mem, OP_IMM); break;
        case 0xC4: cpu_instr_cpy(cpu, mem, OP_ZPG); break;
        case 0xCC: cpu_instr_cpy(cpu, mem, OP_ABS); break;

        case 0x01: cpu_instr_ora(cpu, mem, OP_X_IND); break;
        case 0x05: cpu_instr_ora(cpu, mem, OP_ZPG); break;
        case 0x09: cpu_instr_ora(cpu, mem, OP_IMM); break;
        case 0x0D: cpu_instr_ora(cpu, mem, OP_ABS); break;
        case 0x11: cpu_instr_ora(cpu, mem, OP_IND_Y); break;
        case 0x15: cpu_instr_ora(cpu, mem, OP_ZPG_X); break;
        case 0x19: cpu_instr_ora(cpu, mem, OP_ABS_Y); break;
        case 0x1D: cpu_instr_ora(cpu, mem, OP_ABS_X); break;

        case 0x21: cpu_instr_and(cpu, mem, OP_X_IND); break;
        case 0x25: cpu_instr_and(cpu, mem, OP_ZPG); break;
        case 0x29: cpu_instr_and(cpu, mem, OP_IMM); break;
        case 0x2D: cpu_instr_and(cpu, mem, OP_ABS); break;
        case 0x31: cpu_instr_and(cpu, mem, OP_IND_Y); break;
        case 0x35: cpu_instr_and(cpu, mem, OP_ZPG_X); break;
        case 0x39: cpu_instr_and(cpu, mem, OP_ABS_Y); break;
        case 0x3D: cpu_instr_and(cpu, mem, OP_ABS_X); break;

        case 0x41: cpu_instr_eor(cpu, mem, OP_X_IND); break;
        case 0x45: cpu_instr_eor(cpu, mem, OP_ZPG); break;
        case 0x49: cpu_instr_eor(cpu, mem, OP_IMM); break;
        case 0x4D: cpu_instr_eor(cpu, mem, OP_ABS); break;
        case 0x51: cpu_instr_eor(cpu, mem, OP_IND_Y); break;
        case 0x55: cpu_instr_eor(cpu, mem, OP_ZPG_X); break;
        case 0x59: cpu_instr_eor(cpu, mem, OP_ABS_Y); break;
        case 0x5D: cpu_instr_eor(cpu, mem, OP_ABS_X); break;

        case 0x61: cpu_instr_adc(cpu, mem, OP_X_IND); break;
        case 0x65: cpu_instr_adc(cpu, mem, OP_ZPG); break;
        case 0x69: cpu_instr_adc(cpu, mem, OP_IMM); break;
        case 0x6D: cpu_instr_adc(cpu, mem, OP_ABS); break;
        case 0x71: cpu_instr_adc(cpu, mem, OP_IND_Y); break;
        case 0x75: cpu_instr_adc(cpu, mem, OP_ZPG_X); break;
        case 0x79: cpu_instr_adc(cpu, mem, OP_ABS_Y); break;
        case 0x7D: cpu_instr_adc(cpu, mem, OP_ABS_X); break;

        case 0x81: cpu_instr_sta(cpu, mem, OP_X_IND); break;
        case 0x85: cpu_instr_sta(cpu, mem, OP_ZPG); break;
        case 0x8D: cpu_instr_sta(cpu, mem, OP_ABS); break;
        case 0x91: cpu_instr_sta(cpu, mem, OP_IND_Y); break;
        case 0x95: cpu_instr_sta(cpu, mem, OP_ZPG_X); break;
        case 0x99: cpu_instr_sta(cpu, mem, OP_ABS_Y); break;
        case 0x9D: cpu_instr_sta(cpu, mem, OP_ABS_X); break;

        case 0xA1: cpu_instr_lda(cpu, mem, OP_X_IND); break;
        case 0xA5: cpu_instr_lda(cpu, mem, OP_ZPG); break;
        case 0xA9: cpu_instr_lda(cpu, mem, OP_IMM); break;
        case 0xAD: cpu_instr_lda(cpu, mem, OP_ABS); break;
        case 0xB1: cpu_instr_lda(cpu, mem, OP_IND_Y); break;
        case 0xB5: cpu_instr_lda(cpu, mem, OP_ZPG_X); break;
        case 0xB9: cpu_instr_lda(cpu, mem, OP_ABS_Y); break;
        case 0xBD: cpu_instr_lda(cpu, mem, OP_ABS_X); break;

        case 0xC1: cpu_instr_cmp(cpu, mem, OP_X_IND); break;
        case 0xC5: cpu_instr_cmp(cpu, mem, OP_ZPG); break;
        case 0xC9: cpu_instr_cmp(cpu, mem, OP_IMM); break;
        case 0xCD: cpu_instr_cmp(cpu, mem, OP_ABS); break;
        case 0xD1: cpu_instr_cmp(cpu, mem, OP_IND_Y); break;
        case 0xD5: cpu_instr_cmp(cpu, mem, OP_ZPG_X); break;
        case 0xD9: cpu_instr_cmp(cpu, mem, OP_ABS_Y); break;
        case 0xDD: cpu_instr_cmp(cpu, mem, OP_ABS_X); break;

        case 0xE1: cpu_instr_sbc(cpu, mem, OP_X_IND); break;
        case 0xE5: cpu_instr_sbc(cpu, mem, OP_ZPG); break;
        case 0xE9: cpu_instr_sbc(cpu, mem, OP_IMM); break;
        case 0xED: cpu_instr_sbc(cpu, mem, OP_ABS); break;
        case 0xF1: cpu_instr_sbc(cpu, mem, OP_IND_Y); break;
        case 0xF5: cpu_instr_sbc(cpu, mem, OP_ZPG_X); break;
        case 0xF9: cpu_instr_sbc(cpu, mem, OP_ABS_Y); break;
        case 0xFD: cpu_instr_sbc(cpu, mem, OP_ABS_X); break;

        case 0x06: cpu_instr_asl(cpu, mem, OP_ZPG); break;
        case 0x0A: cpu_instr_asl(cpu, mem, OP_A); break;
        case 0x0E: cpu_instr_asl(cpu, mem, OP_ABS); break;
        case 0x16: cpu_instr_asl(cpu, mem, OP_ZPG_X); break;
        case 0x1E: cpu_instr_asl(cpu, mem, OP_ABS_X); break;

        case 0x26: cpu_instr_rol(cpu, mem, OP_ZPG); break;
        case 0x2A: cpu_instr_rol(cpu, mem, OP_A); break;
        case 0x2E: cpu_instr_rol(cpu, mem, OP_ABS); break;
        case 0x36: cpu_instr_rol(cpu, mem, OP_ZPG_X); break;
        case 0x3E: cpu_instr_rol(cpu, mem, OP_ABS_X); break;

        case 0x46: cpu_instr_lsr(cpu, mem, OP_ZPG); break;
        case 0x4A: cpu_instr_lsr(cpu, mem, OP_A); break;
        case 0x4E: cpu_instr_lsr(cpu, mem, OP_ABS); break;
        case 0x56: cpu_instr_lsr(cpu, mem, OP_ZPG_X); break;
        case 0x5E: cpu_instr_lsr(cpu, mem, OP_ABS_X); break;

        case 0x66: cpu_instr_ror(cpu, mem, OP_ZPG); break;
        case 0x6A: cpu_instr_ror(cpu, mem, OP_A); break;
        case 0x6E: cpu_instr_ror(cpu, mem, OP_ABS); break;
        case 0x76: cpu_instr_ror(cpu, mem, OP_ZPG_X); break;
        case 0x7E: cpu_instr_ror(cpu, mem, OP_ABS_X); break;

        case 0x84: cpu_instr_sty(cpu, mem, OP_ZPG); break;
        case 0x8C: cpu_instr_sty(cpu, mem, OP_ABS); break;
        case 0x94: cpu_instr_sty(cpu, mem, OP_ZPG_X); break;

        case 0x86: cpu_instr_stx(cpu, mem, OP_ZPG); break;
        case 0x8E: cpu_instr_stx(cpu, mem, OP_ABS); break;
        case 0x96: cpu_instr_stx(cpu, mem, OP_ZPG_Y); break;

        case 0xA2: cpu_instr_ldx(cpu, mem, OP_IMM); break;
        case 0xA6: cpu_instr_ldx(cpu, mem, OP_ZPG); break;
        case 0xAE: cpu_instr_ldx(cpu, mem, OP_ABS); break;
        case 0xB6: cpu_instr_ldx(cpu, mem, OP_ZPG_Y); break;
        case 0xBE: cpu_instr_ldx(cpu, mem, OP_ABS_Y); break;

        case 0xA0: cpu_instr_ldy(cpu, mem, OP_IMM); break;
        case 0xA4: cpu_instr_ldy(cpu, mem, OP_ZPG); break;
        case 0xAC: cpu_instr_ldy(cpu, mem, OP_ABS); break;
        case 0xB4: cpu_instr_ldy(cpu, mem, OP_ZPG_X); break;
        case 0xBC: cpu_instr_ldy(cpu, mem, OP_ABS_X); break;

        case 0xC6: cpu_instr_dec(cpu, mem, OP_ZPG); break;
        case 0xCE: cpu_instr_dec(cpu, mem, OP_ABS); break;
        case 0xD6: cpu_instr_dec(cpu, mem, OP_ZPG_X); break;
        case 0xDE: cpu_instr_dec(cpu, mem, OP_ABS_X); break;

        case 0xE6: cpu_instr_inc(cpu, mem, OP_ZPG); break;
        case 0xEE: cpu_instr_inc(cpu, mem, OP_ABS); break;
        case 0xF6: cpu_instr_inc(cpu, mem, OP_ZPG_X); break;
        case 0xFE: cpu_instr_inc(cpu, mem, OP_ABS_X); break;

        case 0x1A: cpu_instr_nop(cpu, mem, OP_IMPL); break;
        case 0x3A: cpu_instr_nop(cpu, mem, OP_IMPL); break;
        case 0x5A: cpu_instr_nop(cpu, mem, OP_IMPL); break;
        case 0x7A: cpu_instr_nop(cpu, mem, OP_IMPL); break;
        case 0xDA: cpu_instr_nop(cpu, mem, OP_IMPL); break;
        case 0xFA: cpu_instr_nop(cpu, mem, OP_IMPL); break;
        case 0x80: cpu_instr_nop(cpu, mem, OP_IMM); break;
        case 0x82: cpu_instr_nop(cpu, mem, OP_IMM); break;
        case 0x89: cpu_instr_nop(cpu, mem, OP_IMM); break;
        case 0xC2: cpu_instr_nop(cpu, mem, OP_IMM); break;
        case 0xE2: cpu_instr_nop(cpu, mem, OP_IMM); break;
        case 0x04: cpu_instr_nop(cpu, mem, OP_ZPG); break;
        case 0x44: cpu_instr_nop(cpu, mem, OP_ZPG); break;
        case 0x64: cpu_instr_nop(cpu, mem, OP_ZPG); break;
        case 0x14: cpu_instr_nop(cpu, mem, OP_ZPG_X); break;
        case 0x34: cpu_instr_nop(cpu, mem, OP_ZPG_X); break;
        case 0x54: cpu_instr_nop(cpu, mem, OP_ZPG_X); break;
        case 0x74: cpu_instr_nop(cpu, mem, OP_ZPG_X); break;
        case 0xD4: cpu_instr_nop(cpu, mem, OP_ZPG_X); break;
        case 0xF4: cpu_instr_nop(cpu, mem, OP_ZPG_X); break;
        case 0x0C: cpu_instr_nop(cpu, mem, OP_ABS); break;
        case 0x1C: cpu_instr_nop(cpu, mem, OP_ABS_X); break;
        case 0x3C: cpu_instr_nop(cpu, mem, OP_ABS_X); break;
        case 0x5C: cpu_instr_nop(cpu, mem, OP_ABS_X); break;
        case 0x7C: cpu_instr_nop(cpu, mem, OP_ABS_X); break;
        case 0xDC: cpu_instr_nop(cpu, mem, OP_ABS_X); break;
        case 0xFC: cpu_instr_nop(cpu, mem, OP_ABS_X); break;

        case 0xA7: cpu_instr_lax(cpu, mem, OP_ZPG); break;
        case 0xB7: cpu_instr_lax(cpu, mem, OP_ZPG_Y); break;
        case 0xAF: cpu_instr_lax(cpu, mem, OP_ABS); break;
        case 0xBF: cpu_instr_lax(cpu, mem, OP_ABS_Y); break;
        case 0xA3: cpu_instr_lax(cpu, mem, OP_X_IND); break;
        case 0xB3: cpu_instr_lax(cpu, mem, OP_IND_Y); break;

        case 0x87: cpu_instr_sax(cpu, mem, OP_ZPG); break;
        case 0x97: cpu_instr_sax(cpu, mem, OP_ZPG_Y); break;
        case 0x8F: cpu_instr_sax(cpu, mem, OP_ABS); break;
        case 0x83: cpu_instr_sax(cpu, mem, OP_X_IND); break;

        case 0xEB: cpu_instr_sbc(cpu, mem, OP_IMM); break;

        case 0xC7: cpu_instr_dcp(cpu, mem, OP_ZPG); break;
        case 0xD7: cpu_instr_dcp(cpu, mem, OP_ZPG_X); break;
        case 0xCF: cpu_instr_dcp(cpu, mem, OP_ABS); break;
        case 0xDF: cpu_instr_dcp(cpu, mem, OP_ABS_X); break;
        case 0xDB: cpu_instr_dcp(cpu, mem, OP_ABS_Y); break;
        case 0xC3: cpu_instr_dcp(cpu, mem, OP_X_IND); break;
        case 0xD3: cpu_instr_dcp(cpu, mem, OP_IND_Y); break;

        case 0xE7: cpu_instr_isc(cpu, mem, OP_ZPG); break;
        case 0xF7: cpu_instr_isc(cpu, mem, OP_ZPG_X); break;
        case 0xEF: cpu_instr_isc(cpu, mem, OP_ABS); break;
        case 0xFF: cpu_instr_isc(cpu, mem, OP_ABS_X); break;
        case 0xFB: cpu_instr_isc(cpu, mem, OP_ABS_Y); break;
        case 0xE3: cpu_instr_isc(cpu, mem, OP_X_IND); break;
        case 0xF3: cpu_instr_isc(cpu, mem, OP_IND_Y); break;

        case 0x27: cpu_instr_rla(cpu, mem, OP_ZPG); break;
        case 0x37: cpu_instr_rla(cpu, mem, OP_ZPG_X); break;
        case 0x2F: cpu_instr_rla(cpu, mem, OP_ABS); break;
        case 0x3F: cpu_instr_rla(cpu, mem, OP_ABS_X); break;
        case 0x3B: cpu_instr_rla(cpu, mem, OP_ABS_Y); break;
        case 0x23: cpu_instr_rla(cpu, mem, OP_X_IND); break;
        case 0x33: cpu_instr_rla(cpu, mem, OP_IND_Y); break;

        case 0x07: cpu_instr_slo(cpu, mem, OP_ZPG); break;
        case 0x17: cpu_instr_slo(cpu, mem, OP_ZPG_X); break;
        case 0x0F: cpu_instr_slo(cpu, mem, OP_ABS); break;
        case 0x1F: cpu_instr_slo(cpu, mem, OP_ABS_X); break;
        case 0x1B: cpu_instr_slo(cpu, mem, OP_ABS_Y); break;
        case 0x03: cpu_instr_slo(cpu, mem, OP_X_IND); break;
        case 0x13: cpu_instr_slo(cpu, mem, OP_IND_Y); break;

        case 0x47: cpu_instr_sre(cpu, mem, OP_ZPG); break;
        case 0x57: cpu_instr_sre(cpu, mem, OP_ZPG_X); break;
        case 0x4F: cpu_instr_sre(cpu, mem, OP_ABS); break;
        case 0x5F: cpu_instr_sre(cpu, mem, OP_ABS_X); break;
        case 0x5B: cpu_instr_sre(cpu, mem, OP_ABS_Y); break;
        case 0x43: cpu_instr_sre(cpu, mem, OP_X_IND); break;
        case 0x53: cpu_instr_sre(cpu, mem, OP_IND_Y); break;

        case 0x67: cpu_instr_rra(cpu, mem, OP_ZPG); break;
        case 0x77: cpu_instr_rra(cpu, mem, OP_ZPG_X); break;
        case 0x6F: cpu_instr_rra(cpu, mem, OP_ABS); break;
        case 0x7F: cpu_instr_rra(cpu, mem, OP_ABS_X); break;
        case 0x7B: cpu_instr_rra(cpu, mem, OP_ABS_Y); break;
        case 0x63: cpu_instr_rra(cpu, mem, OP_X_IND); break;
        case 0x73: cpu_instr_rra(cpu, mem, OP_IND_Y); break;
            // clang-format on

        default:
            PANIC("Invalid opcode (pc = $%04X, opcode = $%02X)\n", pc_start,
                  opcode);
    }
}

void cpu_reset(struct cpu_t cpu[static 1], struct memory_t mem)
{
    cpu->pc = cpu_read_mem_u16(cpu, mem, RES_LOC); // 2 cycles
    cpu->s = 0xFD; // occurs due to reset sequence internals
    cpu->p |= FLAG_I;

    cpu->cyc += 5; // other reset sequence cycles
}

bool cpu_request_irq(struct cpu_t cpu[static 1], struct memory_t mem)
{
    if ((cpu->p & FLAG_I) != 0)
        return false;

    cpu_st_push_u16(cpu, mem, cpu->pc);
    cpu_st_push(cpu, mem, (cpu->p & ~FLAG_B));

    cpu->pc = cpu_read_mem_u16(cpu, mem, IRQ_LOC);
    set_bits(&cpu->p, FLAG_I, true);

    ++cpu->cyc;
    return true;
}

void cpu_request_nmi(struct cpu_t cpu[static 1], struct memory_t mem)
{
    cpu_st_push_u16(cpu, mem, cpu->pc);
    cpu_st_push(cpu, mem, (cpu->p & ~FLAG_B));

    cpu->pc = cpu_read_mem_u16(cpu, mem, NMI_LOC);
    set_bits(&cpu->p, FLAG_I, true);

    ++cpu->cyc;
}
