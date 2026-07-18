#include "nes.h"
#include "cpu.h"
#include "error.h"
#include "ines.h"
#include "mapper.h"
#include "util.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum dma_cycle_t : bool {
    DMA_CYCLE_GET,
    DMA_CYCLE_PUT,
};

enum dma_state_t : u8 {
    DMA_STATE_IDLE,
    DMA_STATE_READ,
    DMA_STATE_WRITE,
};

struct pulse_t {
    union {
        struct {
            u8 vol;
            u8 sweep;
            u8 lo;
            u8 hi;
        };
        u8 data[4];
    };
};

struct triangle_t {
    union {
        struct {
            u8 linear;
            u8 _padding;
            u8 lo;
            u8 hi;
        };
        u8 data[4];
    };
};

struct noise_t {
    union {
        struct {
            u8 vol;
            u8 _padding;
            u8 lo;
            u8 hi;
        };
        u8 data[4];
    };
};

enum dmc_freq_t : u8 {
    DMC_FREQ_FREQ = 0x0F,
    DMC_FREQ_LOOP = 1 << 6,
    DMC_FREQ_IRQ = 1 << 7,
};

struct dmc_t {
    union {
        struct {
            u8 freq;
            u8 raw;
            u8 start;
            u8 len;
        };
        u8 data[4];
    };
};

enum fc_ctrl_t : u8 {
    FC_CTRL_INT = 1 << 6,
    FC_CTRL_MODE = 1 << 7,
};

typedef uint(6) (*frame_buf_mut_t)[NES_SCREEN_WIDTH];

struct nes_t {
    FILE *log_file;

    struct cpu_t cpu;
    u8 *ram;

    // mapper
    struct mapper_t mapper;
    bool mapper_init;

    // ppu
    u64 ppu_cyc;
    u16 hpos;
    u16 vpos;
    u8 *vram;
    u8 *oam;
    frame_buf_mut_t frame_buf;
    uint(6) pal_ram[NES_PAL_RAM_SIZE];
    u8 ppuctrl;
    u8 ppumask;
    u8 ppustatus;
    u8 oamaddr;
    u8 ppudata_buf;
    u16 p0_reg;
    u16 p1_reg;
    u8 attr_next;
    u8 tile_id_next;
    u8 p0_next;
    u8 p1_next;
    u8 pal0_reg;
    u8 pal1_reg;
    uint(1) pal0_latch;
    uint(1) pal1_latch;
    u8 oam_snd[32];
    uint(1) frame_parity;
    u8 sprs_p0[8];
    u8 sprs_p1[8];
    u8 sprs_x[8];
    u8 sprs_attr[8];
    u8 soam_idx;
    bool loaded_spr0;

    uint(15) v;
    uint(15) t;
    uint(3) x;
    uint(1) w;

    // apu
    struct pulse_t sq1;
    struct pulse_t sq2;
    struct triangle_t tri;
    struct noise_t noise;
    struct dmc_t dmc;
    u8 snd_chn_enable;
    u8 fc_ctrl;

    // input
    enum nes_btn_t btns;
    u8 joy_strobe;
    u8 joy1;

    // dma
    enum dma_state_t oamdma_state;
    u64 dma_cyc;
    u8 oamdma_page;
    u8 dma_counter;
    u8 dma_buf;

    // irq sources
    bool irq_apu_dmc;
    bool irq_apu_fc;
};

static constexpr size_t RAM_SIZE = 0x800;
static constexpr size_t VRAM_SIZE = 0x800;
static constexpr size_t OAM_SIZE = 4ULL * 64;

static constexpr size_t CPU_CLK_RATIO = 12;
static constexpr size_t PPU_CLK_RATIO = 4;

enum ppuctrl_t : u8 {
    PPUCTRL_VRAM_ADDR_INC = 1 << 2,
    PPUCTRL_SPR_PAT_ADDR = 1 << 3,
    PPUCTRL_BG_PAT_ADDR = 1 << 4,
    PPUCTRL_SPR_SIZE = 1 << 5,
    PPUCTRL_PPU_MASTER_SLAVE = 1 << 6,
    PPUCTRL_VBLANK_NMI_EN = 1 << 7,
};

enum ppumask_t : u8 {
    PPUMASK_GREYSCALE = 1 << 0,
    PPUMASK_SHOW_BG_LEFT = 1 << 1,
    PPUMASK_SHOW_SPRS_LEFT = 1 << 2,
    PPUMASK_BG_EN = 1 << 3,
    PPUMASK_SPR_EN = 1 << 4,
    PPUMASK_EMP_RED = 1 << 5,
    PPUMASK_EMP_GREEN = 1 << 6,
    PPUMASK_EMP_BLUE = 1 << 7,
};

enum ppustatus_t : u8 {
    PPUSTATUS_SPR_OF = 1 << 5,
    PPUSTATUS_SPR0_HIT = 1 << 6,
    PPUSTATUS_VBLANK = 1 << 7,
};

enum joy_t : u8 {
    JOY_A = 1 << 0,
    JOY_B = 1 << 1,
    JOY_SELECT = 1 << 2,
    JOY_START = 1 << 3,
    JOY_UP = 1 << 4,
    JOY_DOWN = 1 << 5,
    JOY_LEFT = 1 << 6,
    JOY_RIGHT = 1 << 7,
};

u8 nes_read_ppu(struct nes_t *nes, uint(14) addr)
{
    if (addr < 0x3F00) {
        assert(nes->mapper_init);
        auto result = mapper_read_ppu(nes->mapper, addr);

        switch (result.kind) {
            case MAPPER_PPU_READ_DIRECT:
                return result.direct_value;
            case MAPPER_PPU_READ_VRAM:
                assert(result.vram_addr < VRAM_SIZE);
                return nes->vram[result.vram_addr];
        }

        unreachable();
    }

    return nes->pal_ram[(addr - 0x4000) % NES_PAL_RAM_SIZE];
}

static void nes_write_ppu(struct nes_t nes[static 1], uint(14) addr, u8 value)
{

    if (addr < 0x3F00) {
        assert(nes->mapper_init);
        auto result = mapper_write_ppu(nes->mapper, addr, value);

        switch (result.kind) {
            case MAPPER_PPU_WRITE_DONE:
                return;
            case MAPPER_PPU_WRITE_VRAM:
                nes->vram[result.vram.addr] = result.vram.value;
                return;
        }

        unreachable();
    }

    u16 pal_addr = (addr - 0x4000) % NES_PAL_RAM_SIZE;

    if ((pal_addr % 4) == 0) {
        // entry 0s are shared between background and sprite palettes
        // See https://www.nesdev.org/wiki/PPU_palettes#Palette_RAM
        nes->pal_ram[pal_addr & 0x0F] = value;
        nes->pal_ram[pal_addr | 0x10] = value;
    } else {
        nes->pal_ram[pal_addr] = value;
    }
}

static void nes_update_joy1(struct nes_t nes[static 1])
{
    nes->joy1 = nes->btns;
}

static void nes_inc_ppuaddr(struct nes_t nes[static 1])
{
    nes->v += (nes->ppuctrl & PPUCTRL_VRAM_ADDR_INC) == 0 ? 1 : 32;
}

static u8 nes_read_mem(struct nes_t nes[static 1], u16 addr)
{
    if (addr < 0x2000)
        return nes->ram[addr % RAM_SIZE];

    if (addr < 0x4000) {
        switch ((addr - 0x2000) % 8) {
            case 0:
                PANIC("read from $%04X (ppuctrl)", addr);
            case 1:
                return 0xFF;
            case 2: {
                u8 val = nes->ppustatus;
                set_bits(&nes->ppustatus, PPUSTATUS_VBLANK, false);
                nes->w = 0;
                return val;
            }
            case 3: // oamaddr
                PANIC("read from $%04X (oamaddr)", addr);
            case 4: // oamdata
                return nes->oam[nes->oamaddr];
            case 5: // ppuscroll
                PANIC("read from $%04X (ppuscroll)", addr);
            case 6: // ppuaddr
                PANIC("read from $%04X (ppuaddr)", addr);
            case 7: {
                // ppudata is delayed by a buffer
                u8 data = nes->ppudata_buf;
                nes->ppudata_buf = nes_read_ppu(nes, nes->v);
                nes_inc_ppuaddr(nes);
                return data;
            }
            default:
                unreachable();
        }
    }

    if (addr < 0x4018) {
        switch (addr) {
            case 0x4014: // oamdma
                PANIC("read from $%04X (oamdma)", addr);
            case 0x4015: // apu frame counter control
                nes->irq_apu_fc = false;
                return 0xFF;
            case 0x4016: { // joy1
                if (nes->joy_strobe != 0)
                    nes_update_joy1(nes);

                u8 val = nes->joy1 & 1;
                nes->joy1 = (nes->joy1 >> 1) | 0x80;

                return val;
            }
            case 0x4017:
                return 0x00;
            default:
                PANIC("read from $%04X", addr);
        }
    }

    if (addr < 0x4020)
        return 0xFF;

    assert(nes->mapper_init);
    return mapper_read(nes->mapper, addr);
}

static u8 nes_peek_mem(const struct nes_t nes[static 1], u16 addr)
{
    if (addr < 0x2000)
        return nes->ram[addr % RAM_SIZE];

    if (addr < 0x4000) {
        switch ((addr - 0x2000) % 8) {
            case 0:
                return 0xFF;
            case 1:
                return 0xFF;
            case 2:
                return nes->ppustatus;
            case 3: // oamaddr
                return 0xFF;
            case 4: // oamdata
                return nes->oam[nes->oamaddr];
            case 5: // ppuscroll
                return 0xFF;
            case 6: // ppuaddr
                return 0xFF;
            case 7:
                return nes->ppudata_buf;
            default:
                unreachable();
        }
    }

    if (addr < 0x4018) {
        switch (addr) {
            case 0x4014: // oamdma
                return 0xFF;
            case 0x4015: // apu frame counter control
                return 0xFF;
            case 0x4016: // joy1
                if (nes->joy_strobe != 0)
                    return nes->btns & 1;

                return nes->joy1 & 1;
            case 0x4017:
                return 0x00;
            default:
                return 0xFF;
        }
    }

    if (addr < 0x4020)
        return 0xFF;

    assert(nes->mapper_init);
    return mapper_peek(nes->mapper, addr);
}

static void nes_write_mem(struct nes_t nes[static 1], u16 addr, u8 value)
{
    if (addr < 0x2000) {
        nes->ram[addr % RAM_SIZE] = value;
        return;
    }

    if (addr < 0x4000) {
        switch ((addr - 0x2000) % 8) {
            case 0:
                // t: ...GH.. ........ <- d: ......GH
                nes->t = (nes->t & ~(0b11 << 10)) | (((u16)value & 0b11) << 10);

                //    <used elsewhere> <- d: ABCDEF..
                nes->ppuctrl = value & ~0b11;
                return;
            case 1:
                nes->ppumask = value;
                return;
            case 2:
                nes->ppustatus = value;
                return;
            case 3: // oamaddr
                nes->oamaddr = value;
                return;
            case 4: // oamdata
                nes->oam[nes->oamaddr++] = value;
                return;
            case 5: // ppuscroll
                // https://www.nesdev.org/wiki/PPU_scrolling#Register_controls
                if (nes->w == 0) {
                    // t: ....... ...ABCDE <- d: ABCDE...
                    nes->t = (nes->t & ~0b11111) | (value >> 3);

                    // x:              FGH <- d: .....FGH
                    nes->x = value & 0b111;

                    // w:                  <- 1
                    nes->w = 1;
                } else {
                    // t: FGH..AB CDE..... <- d: ABCDEFGH
                    nes->t =
                        (nes->t & ~(0b11111 << 5)) | (((u16)value >> 3) << 5);
                    nes->t = (nes->t & ~(0b111 << 12)) |
                             (((u16)value & 0b111) << 12);

                    // w:                  <- 0
                    nes->w = 0;
                }
                return;
            case 6: // ppuaddr
                if (nes->w == 0) {
                    // t: .CDEFGH ........ <- d: ..CDEFGH
                    //        <unused>     <- d: AB......
                    // t: Z...... ........ <- 0 (bit Z is cleared)
                    nes->t = (nes->t & 0x00FF) | (((u16)value & 0x3F) << 8);

                    // w:                  <- 1
                    nes->w = 1;
                } else {
                    // t: ....... ABCDEFGH <- d: ABCDEFGH
                    nes->t = (nes->t & 0xFF00) | value;

                    // w:                  <- 0
                    nes->w = 0;

                    //    (wait 1 to 1.5 dots after the write completes)
                    // v: <...all bits...> <- t: <...all bits...>
                    nes->v = nes->t; // TODO: delay this by 1-1.5 dots
                }
                return;
            case 7:
                nes_write_ppu(nes, nes->v, value);
                nes_inc_ppuaddr(nes);
                return;
            default:
                unreachable();
        }
    }

    if (addr < 0x4004) { // sq1
        nes->sq1.data[addr - 0x4000] = value;
        return;
    }

    if (addr < 0x4008) { // sq2
        nes->sq2.data[addr - 0x4004] = value;
        return;
    }

    if (addr < 0x400C) { // tri
        nes->tri.data[addr - 0x4008] = value;
        return;
    }

    if (addr < 0x4010) { // noise
        nes->noise.data[addr - 0x400C] = value;
        return;
    }

    if (addr < 0x4014) { // dmc
        nes->dmc.data[addr - 0x4010] = value;

        if (addr == 0x4010 && (nes->dmc.freq & DMC_FREQ_IRQ) == 0)
            nes->irq_apu_dmc = false;

        return;
    }

    if (addr < 0x4018) {
        switch (addr) {
            case 0x4014: // oamdma
                assert(nes->oamaddr == 0);

                nes->oamdma_page = value;
                nes->oamdma_state = DMA_STATE_READ;
                nes->dma_counter = 0;
                return;
            case 0x4015:
                nes->snd_chn_enable = value;
                nes->irq_apu_dmc = false;
                return;
            case 0x4016: // joy1
                nes->joy_strobe = value & 1;

                if (nes->joy_strobe != 0)
                    nes_update_joy1(nes);

                return;
            case 0x4017:
                nes->fc_ctrl = value;
                return;
            default:
                PANIC("write to $%04X (value = $%02X)", addr, value);
        }
    }

    if (addr < 0x4020)
        return;

    assert(nes->mapper_init);
    mapper_write(nes->mapper, addr, value);
}

struct nes_t *nes_init(struct nes_t *nes, FILE *log_file)
{
    if (nes == nullptr)
        return nullptr;

    u8 *ram = calloc(RAM_SIZE, sizeof(ram[0]));
    if (ram == nullptr)
        goto err_cleanup_1;

    u8 *vram = calloc(VRAM_SIZE, sizeof(vram[0]));
    if (vram == nullptr)
        goto err_cleanup_2;

    frame_buf_mut_t frame_buf = calloc(NES_SCREEN_HEIGHT, sizeof(frame_buf[0]));
    if (frame_buf == nullptr)
        goto err_cleanup_3;

    u8 *oam = calloc(OAM_SIZE, sizeof(oam[0]));
    if (oam == nullptr)
        goto err_cleanup_4;

    struct cpu_t cpu = {};
    if (!cpu_init(&cpu))
        goto err_cleanup_5;

    *nes = (struct nes_t){};
    nes->log_file = log_file;
    nes->cpu = cpu;
    nes->ram = ram;
    nes->vram = vram;
    nes->oam = oam;
    nes->frame_buf = frame_buf;

    return nes;

err_cleanup_5:
    free(oam);
err_cleanup_4:
    free(frame_buf);
err_cleanup_3:
    free(vram);
err_cleanup_2:
    free(ram);
err_cleanup_1:
    return nullptr;
}

struct nes_t *nes_create(FILE *log_file)
{
    return nes_init(calloc(1, sizeof(struct nes_t)), log_file);
}

void nes_deinit(struct nes_t *nes)
{
    if (nes == nullptr)
        return;

    if (nes->mapper_init)
        mapper_deinit(nes->mapper);

    free(nes->oam);
    free(nes->frame_buf);
    free(nes->vram);
    free(nes->ram);

    *nes = (struct nes_t){};
}

void nes_destroy(struct nes_t *nes)
{
    nes_deinit(nes);
    free(nes);
}

enum nes_error_t nes_load_rom(struct nes_t *nes,
                              const struct ines_t ines[static 1])
{
    if (nes->mapper_init) {
        mapper_deinit(nes->mapper);
        nes->mapper_init = false;
    }

    NES_TRY(mapper_from_rom(ines, &nes->mapper));
    nes->mapper_init = true;

    return NES_OK;
}

static void nes_deinit_v(void *ptr)
{
    nes_deinit(ptr);
}

static u8 nes_read_mem_v(void *ptr, u16 addr)
{
    return nes_read_mem(ptr, addr);
}

static u8 nes_peek_mem_v(const void *ptr, u16 addr)
{
    return nes_peek_mem(ptr, addr);
}

static void nes_write_mem_v(void *ptr, u16 addr, u8 value)
{
    nes_write_mem(ptr, addr, value);
}

static const struct memory_vtable_t NES_MEMORY_VTABLE = {
    .deinit = nes_deinit_v,
    .read = nes_read_mem_v,
    .peek = nes_peek_mem_v,
    .write = nes_write_mem_v,
};

static struct memory_t nes_as_memory(struct nes_t nes[static 1])
{
    return (struct memory_t){
        .ptr = nes,
        .vtable = &NES_MEMORY_VTABLE,
    };
}

void nes_reset(struct nes_t *nes)
{
    cpu_reset(&nes->cpu, nes_as_memory(nes));
}

void nes_set_pc(struct nes_t *nes, u16 pc)
{
    nes->cpu.pc = pc;
}

void nes_set_btn(struct nes_t *nes, enum nes_btn_t btn, bool pressed)
{
    set_bits(&nes->btns, 1 << btn, pressed);
}

static constexpr size_t DOTS_X = 341;
static constexpr size_t DOTS_Y = 262;

static_assert(DOTS_X >= NES_SCREEN_WIDTH);
static_assert(DOTS_Y >= NES_SCREEN_HEIGHT);

static constexpr size_t LINE_POST_RENDER = NES_SCREEN_HEIGHT;
static constexpr size_t LINE_VBLANK = LINE_POST_RENDER + 1;
static constexpr size_t LINE_PRE_RENDER = DOTS_Y - 1;

enum spr_attrs_t : u8 {
    ATTRS_PALETTE = 0b11,
    ATTRS_PRIORITY = 1 << 5,
    ATTRS_FLIP_HORIZONTAL = 1 << 6,
    ATTRS_FLIP_VERTICAL = 1 << 7,
};

enum spr_priority_t : u8 {
    PRIORITY_HIGH,
    PRIORITY_LOW,
};

static void nes_log_trace(struct nes_t nes[static 1])
{
    if (nes->log_file == nullptr)
        return;

    auto trace = nes->cpu.trace;
    fprintf(nes->log_file, "%04X  ", trace.pc);

    for (size_t i = 0; i < CPU_INSTR_BYTES_MAX; ++i) {
        if (i < trace.instr.bytes_cnt)
            fprintf(nes->log_file, "%02X ", trace.instr.bytes[i]);
        else
            fprintf(nes->log_file, "   ");
    }

    fprintf(nes->log_file, "%c%.3s ", trace.instr.illegal ? '*' : ' ',
            trace.instr.mnemonic);

    char buf[29] = {};
    auto op = trace.instr.op;

    switch (op.kind) {
        case OP_IMPL:
            break;
        case OP_A:
            snprintf(buf, sizeof(buf), "A");
            break;
        case OP_ABS:
            snprintf(buf, sizeof(buf), "$%04X = %02X", op.abs.addr,
                     op.abs.addr_value);
            break;
        case OP_ADDR:
            snprintf(buf, sizeof(buf), "$%04X", op.addr.addr);
            break;
        case OP_ABS_X:
            snprintf(buf, sizeof(buf), "$%04X,X @ %04X = %02X", op.abs_xy.addr,
                     op.abs_xy.addr_xy, op.abs_xy.addr_xy_value);
            break;
        case OP_ABS_Y:
            snprintf(buf, sizeof(buf), "$%04X,Y @ %04X = %02X", op.abs_xy.addr,
                     op.abs_xy.addr_xy, op.abs_xy.addr_xy_value);
            break;
        case OP_IMM:
            snprintf(buf, sizeof(buf), "#$%02X", op.imm.value);
            break;
        case OP_IND:
            snprintf(buf, sizeof(buf), "($%04X) = %04X", op.ind.addr,
                     op.ind.eff_addr);
            break;
        case OP_X_IND:
            snprintf(buf, sizeof(buf), "($%02X,X) @ %02X = %04X = %02X",
                     op.x_ind.addr, op.x_ind.addr_x, op.x_ind.eff_addr,
                     op.x_ind.eff_addr_value);
            break;
        case OP_IND_Y:
            snprintf(buf, sizeof(buf), "($%02X),Y = %04X @ %04X = %02X",
                     op.ind_y.addr, op.ind_y.eff_addr, op.ind_y.eff_addr_y,
                     op.ind_y.eff_addr_y_value);
            break;
        case OP_ZPG:
            snprintf(buf, sizeof(buf), "$%02X = %02X", op.zpg.addr,
                     op.zpg.addr_value);
            break;
        case OP_ZPG_X:
            snprintf(buf, sizeof(buf), "$%02X,X @ %02X = %02X", op.zpg_xy.addr,
                     op.zpg_xy.addr_xy, op.zpg_xy.addr_xy_value);
            break;
        case OP_ZPG_Y:
            snprintf(buf, sizeof(buf), "$%02X,Y @ %02X = %02X", op.zpg_xy.addr,
                     op.zpg_xy.addr_xy, op.zpg_xy.addr_xy_value);
            break;
    }

    fprintf(nes->log_file,
            "%-*sA:%02X X:%02X Y:%02X P:%02X SP:%02X PPU:%3u,%3u CYC:%zu\n",
            (int)(sizeof(buf) - 1), buf, trace.a, trace.x, trace.y, trace.p,
            trace.s, nes->vpos, nes->hpos, trace.cyc);
    fflush(nes->log_file);
}

u64 nes_time_cpu(const struct nes_t *nes)
{
    return CPU_CLK_RATIO * nes->cpu.cyc;
}

void nes_dispatch_cpu(struct nes_t *nes)
{
    if (nes->oamdma_state != DMA_STATE_IDLE) {
        // cpu is halted during oam dma
        ++nes->cpu.cyc;
        return;
    }

    bool irq = nes->irq_apu_dmc || nes->irq_apu_fc;

    if (irq) {
        printf("irq\n");
        cpu_request_irq(&nes->cpu, nes_as_memory(nes));
    }

    cpu_step(&nes->cpu, nes_as_memory(nes));

    nes_log_trace(nes);
}

u64 nes_time_pixel(const struct nes_t *nes)
{
    return PPU_CLK_RATIO * nes->ppu_cyc;
}

// Source: https://www.nesdev.org/wiki/PPU_scrolling#Wrapping_around
static uint(15) coarse_x_increment(uint(15) v)
{
    if ((v & 0x001F) == 31) { // if coarse X == 31
        v &= ~0x001F; // coarse X = 0
        v ^= 0x0400; // switch horizontal nametable
    } else {
        v += 1; // increment coarse X
    }

    return v;
}

// Source: https://www.nesdev.org/wiki/PPU_scrolling#Wrapping_around
static uint(15) y_increment(uint(15) v)
{
    if ((v & 0x7000) != 0x7000) { // if fine Y < 7
        v += 0x1000; // increment fine Y
    } else {
        v &= ~0x7000; // fine Y = 0
        uint(15) y = (v & 0x03E0) >> 5; // let y = coarse Y
        if (y == 29) {
            y = 0; // coarse Y = 0
            v ^= 0x0800; // switch vertical nametable
        } else if (y == 31) {
            y = 0; // coarse Y = 0, nametable not switched
        } else {
            y += 1; // increment coarse Y
        }
        v = (v & ~0x03E0) | (y << 5); // put coarse Y back into v
    }

    return v;
}

void nes_dispatch_pixel(struct nes_t *nes)
{
    bool ppu_enabled = (nes->ppumask & (PPUMASK_BG_EN | PPUMASK_SPR_EN)) != 0;

    bool hpos_visible = nes->hpos > 0 && nes->hpos <= NES_SCREEN_WIDTH;

    bool vpos_visible = nes->vpos < LINE_POST_RENDER;
    bool vpos_vblank = nes->vpos == LINE_VBLANK;
    bool vpos_pre_render = nes->vpos == LINE_PRE_RENDER;
    bool vpos_visible_or_pre_render = vpos_visible || vpos_pre_render;

    u8 col_mask = (nes->ppumask & PPUMASK_GREYSCALE) == 0 ? 0xFF : 0x30;

    u8 coarse_x = nes->v & 0x1F;
    u8 coarse_y = (nes->v >> 5) & 0x1F;
    u8 fine_y = (nes->v >> 12) & 0b111;

    bool reload_regs = false;
    bool shift_regs = false;

    if (ppu_enabled && vpos_visible_or_pre_render &&
        (hpos_visible || (nes->hpos >= 321 && nes->hpos <= 336))) {
        shift_regs = true;
        u16 pat_base = (((u16)nes->ppuctrl & PPUCTRL_BG_PAT_ADDR) << 8) |
                       ((u16)nes->tile_id_next << 4);

        switch (nes->hpos % 8) {
            case 2: { // fetch from nametable
                u16 tile_addr = 0x2000 | (nes->v & 0x0FFF);
                nes->tile_id_next = nes_read_ppu(nes, tile_addr);
                break;
            }
            case 4: { // fetch from attribute table
                u16 attr_addr = 0x23C0 | (nes->v & 0x0C00) |
                                ((nes->v >> 4) & 0x38) | ((nes->v >> 2) & 0x07);

                nes->attr_next = nes_read_ppu(nes, attr_addr);
                break;
            }
            case 6: // fetch bg plane 0
                nes->p0_next = nes_read_ppu(nes, pat_base + fine_y);
                break;
            case 0: // fetch bg plane 1
                nes->p1_next = nes_read_ppu(nes, pat_base + fine_y + 8);

                if (hpos_visible || nes->hpos >= 328) {
                    nes->v = coarse_x_increment(nes->v);
                    reload_regs = true;
                }
                break;
            default:
                break;
        }
    }

    if (ppu_enabled && vpos_visible_or_pre_render && nes->hpos == 256)
        nes->v = y_increment(nes->v);

    if (ppu_enabled && vpos_visible_or_pre_render && nes->hpos == 257) {
        // v: ....A.. ...BCDEF <- t: ....A.. ...BCDEF
        nes->v = (nes->v & ~0x041F) | (nes->t & 0x041F);
    }

    if (ppu_enabled && vpos_pre_render && nes->hpos >= 280 &&
        nes->hpos <= 304) {
        // v: GHIA.BC DEF..... <- t: GHIA.BC DEF.....
        nes->v = (nes->v & ~0x7BE0) | (nes->t & 0x7BE0);
    }

    if (vpos_visible && hpos_visible) {
        if (!ppu_enabled) {
            nes->frame_buf[nes->vpos][nes->hpos - 1] = 0x1D;
        } else {
            u8 p_b0 = (nes->p0_reg >> (15 - nes->x)) & 1;
            u8 p_b1 = (nes->p1_reg >> (15 - nes->x)) & 1;
            u8 pal_b0 = (nes->pal0_reg >> (7 - nes->x)) & 1;
            u8 pal_b1 = (nes->pal1_reg >> (7 - nes->x)) & 1;

            u8 bg_pixel = p_b0 | (p_b1 << 1);
            u8 bg_pal = pal_b0 | (pal_b1 << 1);

            u8 spr_idx = 0xFF;
            u8 spr_pixel = 0;
            u8 spr_attr = 0;

            for (size_t i = 0; i < 8; ++i) {
                u8 x_pos = nes->sprs_x[i];
                u8 attr = nes->sprs_attr[i];

                if (nes->hpos - 1 >= x_pos && nes->hpos - 1 < (u16)x_pos + 8) {
                    u8 rel_pos = nes->hpos - 1 - (size_t)x_pos;

                    u8 b0 = (nes->sprs_p0[i] >> (7 - rel_pos)) & 1;
                    u8 b1 = (nes->sprs_p1[i] >> (7 - rel_pos)) & 1;
                    u8 pixel = b0 | (b1 << 1);

                    if (pixel != 0) {
                        spr_idx = i;
                        spr_pixel = pixel;
                        spr_attr = attr;
                        break;
                    }
                }
            }

            u8 col = 0;

            if (nes->loaded_spr0 && spr_idx == 0 && bg_pixel != 0 &&
                spr_pixel != 0)
                nes->ppustatus |= PPUSTATUS_SPR0_HIT;

            if (bg_pixel == 0 && spr_pixel == 0) {
                // ext
                col = nes->pal_ram[0];
            } else if (bg_pixel != 0 &&
                       (spr_pixel == 0 || (spr_attr & ATTRS_PRIORITY) != 0)) {
                // bg
                col = nes->pal_ram[(bg_pal << 2) | bg_pixel];
            } else {
                // sprite
                u8 spr_pal = spr_attr & ATTRS_PALETTE;
                col = nes->pal_ram[0x10 | (spr_pal << 2) | spr_pixel];
            }

            nes->frame_buf[nes->vpos][nes->hpos - 1] = col & col_mask;
        }
    }

    if (shift_regs) {
        nes->p0_reg = (nes->p0_reg << 1) | 1;
        nes->p1_reg = (nes->p1_reg << 1) | 1;
        nes->pal0_reg = (nes->pal0_reg << 1) | nes->pal0_latch;
        nes->pal1_reg = (nes->pal1_reg << 1) | nes->pal1_latch;
    }

    if (reload_regs) {
        nes->p0_reg = (nes->p0_reg & 0xFF00) | nes->p0_next;
        nes->p1_reg = (nes->p1_reg & 0xFF00) | nes->p1_next;

        size_t attr_quad = (coarse_y & 0b10) + ((coarse_x >> 1) & 1);
        u8 pal_next = (nes->attr_next >> (2 * attr_quad)) & 0b11;
        nes->pal0_latch = pal_next & 1;
        nes->pal1_latch = (pal_next >> 1) & 1;
    }

    // sprite evaluation =======================================================

    if (ppu_enabled && nes->hpos >= 1) {
        if (nes->hpos <= 64) {
            // clear secondary oam
            if ((nes->hpos % 2) == 0)
                nes->oam_snd[(nes->hpos / 2) - 1] = 0xFF;
        } else if (nes->hpos <= 256) {
            // sprite evaluation
            // TODO: do timing

            nes->soam_idx = 0;
            nes->loaded_spr0 = false;

            for (size_t i = 0; i < 256 && nes->soam_idx < 32; i += 4) {
                u8 y = nes->oam[i];
                nes->oam_snd[nes->soam_idx] = y;

                size_t spr_height =
                    (nes->ppuctrl & PPUCTRL_SPR_SIZE) == 0 ? 8 : 16;

                if (y <= nes->vpos && nes->vpos < y + spr_height) {
                    if (nes->soam_idx == 0)
                        nes->loaded_spr0 = true;

                    nes->oam_snd[++nes->soam_idx] = nes->oam[i + 1];
                    nes->oam_snd[++nes->soam_idx] = nes->oam[i + 2];
                    nes->oam_snd[++nes->soam_idx] = nes->oam[i + 3];
                    ++nes->soam_idx;
                }
            }

            nes->soam_idx = 0;
        } else if (nes->hpos <= 320) {
            // sprite pattern fetches
            u8 *spr_data = &nes->oam_snd[4UL * nes->soam_idx];
            u8 y_pos = spr_data[0];
            u8 tile_id = spr_data[1];
            u8 attr = spr_data[2];
            u8 x_pos = spr_data[3];

            bool flip_hor = (attr & ATTRS_FLIP_HORIZONTAL) != 0;
            bool flip_ver = (attr & ATTRS_FLIP_VERTICAL) != 0;

            size_t spr_height = (nes->ppuctrl & PPUCTRL_SPR_SIZE) == 0 ? 8 : 16;
            u16 pat_addr = 0;

            u8 rel_y = nes->vpos - y_pos;
            u8 rel_y_real = 0;

            if ((nes->ppuctrl & PPUCTRL_SPR_SIZE) == 0) {
                pat_addr = (((u16)nes->ppuctrl & PPUCTRL_SPR_PAT_ADDR) << 9) |
                           ((u16)tile_id << 4);
                rel_y_real = flip_ver ? (spr_height - 1 - rel_y) : rel_y;
            } else {
                u16 pat_base = ((u16)tile_id & 1) << 12;
                u16 tile_id_real =
                    (rel_y < 8) ^ flip_ver ? (tile_id & ~1) : (tile_id | 1);
                pat_addr = pat_base | (tile_id_real << 4);
                rel_y_real = flip_ver ? (7 - (rel_y % 8)) : rel_y % 8;
            }

            if ((nes->hpos % 8) == 6) {
                // fetch plane 0
                nes->sprs_x[nes->soam_idx] = x_pos;
                nes->sprs_attr[nes->soam_idx] = attr;
                nes->sprs_p0[nes->soam_idx] =
                    nes_read_ppu(nes, pat_addr + rel_y_real);

                if (flip_hor) {
                    nes->sprs_p0[nes->soam_idx] =
                        reverse_bits(nes->sprs_p0[nes->soam_idx]);
                }

            } else if ((nes->hpos % 8) == 0) {
                // fetch plane 1
                nes->sprs_p1[nes->soam_idx] =
                    nes_read_ppu(nes, pat_addr + rel_y_real + 8);

                if (flip_hor) {
                    nes->sprs_p1[nes->soam_idx] =
                        reverse_bits(nes->sprs_p1[nes->soam_idx]);
                }

                ++nes->soam_idx;
            }
        }
    }

    if (vpos_vblank && nes->hpos == 1) {
        nes->ppustatus |= PPUSTATUS_VBLANK;

        if ((nes->ppuctrl & PPUCTRL_VBLANK_NMI_EN) != 0)
            cpu_request_nmi(&nes->cpu, nes_as_memory(nes));
    }

    if (vpos_pre_render && nes->hpos == 1) {
        nes->ppustatus &=
            ~(PPUSTATUS_VBLANK | PPUSTATUS_SPR0_HIT | PPUSTATUS_SPR_OF);
    }

    // increment counters
    if (++nes->hpos == DOTS_X) {
        nes->hpos = 0;

        if (++nes->vpos == DOTS_Y) {
            nes->frame_parity ^= 1;
            nes->vpos = 0;
        }
    }

    // skip last dot on odd frames
    if (nes->vpos == DOTS_Y - 1 && nes->hpos == DOTS_X - 1 &&
        nes->frame_parity == 1) {
        nes->frame_parity ^= 1;
        nes->vpos = 0;
        nes->hpos = 0;
    }

    ++nes->ppu_cyc;
}

u64 nes_time_dma_cycle(const struct nes_t *nes)
{
    return CPU_CLK_RATIO * nes->dma_cyc;
}

void nes_dispatch_dma_cycle(struct nes_t *nes)
{
    switch (nes->oamdma_state) {
        case DMA_STATE_IDLE:
            break;
        case DMA_STATE_READ:
            if ((nes->dma_cyc % 2) == DMA_CYCLE_GET) {
                u16 src_addr = ((u16)nes->oamdma_page << 8) | nes->dma_counter;
                nes->dma_buf = nes_read_mem(nes, src_addr);
                nes->oamdma_state = DMA_STATE_WRITE;
            }
            break;
        case DMA_STATE_WRITE:
            assert((nes->dma_cyc % 2) == DMA_CYCLE_PUT);
            nes->oam[nes->dma_counter++] = nes->dma_buf;

            if (nes->dma_counter == 0)
                nes->oamdma_state = DMA_STATE_IDLE;
            else
                nes->oamdma_state = DMA_STATE_READ;
            break;
    }

    ++nes->dma_cyc;
}

nes_frame_buf_t nes_get_frame_buf(const struct nes_t *nes)
{
    return nes->frame_buf;
}
