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

struct nes_t {
    struct cpu_t cpu;
    u8 *ram;

    // mapper
    struct mapper_t mapper;
    bool mapper_init;

    // ppu
    size_t px;
    size_t py;
    u8 *vram;
    u8 *oam;
    u8 (*scanout_buf)[NES_SCREEN_WIDTH];
    u16 ppuaddr;
    u8 pal_ram[NES_PAL_RAM_SIZE];
    u8 ppuctrl;
    u8 ppumask;
    u8 ppustatus;
    u8 oamaddr;
    u8 ppuscroll;
    u8 ppudata_buf;
    unsigned _BitInt(1) w;

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
    enum dma_cycle_t dma_cycle;
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
    PPUCTRL_BASE_NT_ADDR = 0b11,
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

u8 nes_read_ppu(struct nes_t *nes, u16 addr)
{
    if (addr < 0x3F00) {
        assert(nes->mapper_init);
        auto result = mapper_read_ppu(nes->mapper, addr);

        switch (result.kind) {
            case MAPPER_READ_DIRECT:
                return result.direct_value;
            case MAPPER_READ_VRAM:
                return nes->vram[result.vram_addr];
        }

        unreachable();
    }

    if (addr < 0x4000)
        return nes->pal_ram[(addr - 0x4000) % NES_PAL_RAM_SIZE];

    PANIC("ppu read from $%04X", addr);
}

static void nes_write_ppu(struct nes_t nes[static 1], u16 addr, u8 value)
{

    if (addr < 0x3F00) {
        assert(nes->mapper_init);
        auto result = mapper_write_ppu(nes->mapper, addr, value);

        switch (result.kind) {
            case MAPPER_WRITE_DONE:
                return;
            case MAPPER_WRITE_VRAM:
                nes->vram[result.vram.addr] = result.vram.value;
                return;
        }

        unreachable();
    }

    if (addr < 0x4000) {
        u16 pal_addr = (addr - 0x4000) % NES_PAL_RAM_SIZE;

        if ((pal_addr % 4) == 0) {
            // entry 0s are shared between background and sprite palettes
            // See https://www.nesdev.org/wiki/PPU_palettes#Palette_RAM
            nes->pal_ram[pal_addr & 0x0F] = value;
            nes->pal_ram[pal_addr | 0x10] = value;
        } else {
            nes->pal_ram[pal_addr] = value;
        }

        return;
    }

    PANIC("ppu write to $%04X (value = $%02X)", addr, value);
}

static void nes_update_joy1(struct nes_t nes[static 1])
{
    nes->joy1 = nes->btns;
}

static void nes_inc_ppuaddr(struct nes_t nes[static 1])
{
    nes->ppuaddr += (nes->ppuctrl & PPUCTRL_VRAM_ADDR_INC) == 0 ? 1 : 32;
}

static u8 nes_read_mem(struct nes_t nes[static 1], u16 addr)
{
    if (addr < 0x2000)
        return nes->ram[addr % RAM_SIZE];

    if (addr < 0x4000) {
        switch ((addr - 0x2000) % 8) {
            case 0:
                return nes->ppuctrl;
            case 1:
                return nes->ppumask;
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
            case 6:
                PANIC("read from $%04X (ppuaddr)", addr);
            case 7: {
                // ppudata is delayed by a buffer
                u8 data = nes->ppudata_buf;
                nes->ppudata_buf = nes_read_ppu(nes, nes->ppuaddr);
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

static void nes_write_mem(struct nes_t nes[static 1], u16 addr, u8 value)
{
    if (addr < 0x2000) {
        nes->ram[addr % RAM_SIZE] = value;
        return;
    }

    if (addr < 0x4000) {
        switch ((addr - 0x2000) % 8) {
            case 0:
                nes->ppuctrl = value;
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
                if (nes->w == 0)
                    nes->ppuscroll =
                        (nes->ppuscroll & 0x00FF) | ((u16)value << 8);
                else
                    nes->ppuscroll = (nes->ppuscroll & 0xFF00) | value;

                nes->w ^= 1;
                return;
            case 6:
                if (nes->w == 0)
                    nes->ppuaddr = (nes->ppuaddr & 0x00FF) | ((u16)value << 8);
                else
                    nes->ppuaddr = (nes->ppuaddr & 0xFF00) | value;

                nes->w ^= 1;
                return;
            case 7:
                nes_write_ppu(nes, nes->ppuaddr, value);
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

    if (addr == 0x6000) {
        printf("status: $%02X\n", value);
    }

    assert(nes->mapper_init);
    mapper_write(nes->mapper, addr, value);
}

struct nes_t *nes_init(struct nes_t *nes)
{
    if (nes == nullptr)
        return nullptr;

    u8 *ram = calloc(RAM_SIZE, sizeof(ram[0]));
    if (ram == nullptr)
        goto err_cleanup_1;

    u8 *vram = calloc(VRAM_SIZE, sizeof(vram[0]));
    if (vram == nullptr)
        goto err_cleanup_2;

    u8(*scanout_buf)[NES_SCREEN_WIDTH] =
        calloc(NES_SCREEN_HEIGHT, sizeof(scanout_buf[0]));
    if (scanout_buf == nullptr)
        goto err_cleanup_3;

    u8 *oam = calloc(OAM_SIZE, sizeof(oam[0]));
    if (oam == nullptr)
        goto err_cleanup_4;

    struct cpu_t cpu = {};
    if (!cpu_init(&cpu))
        goto err_cleanup_5;

    *nes = (struct nes_t){};
    nes->cpu = cpu;
    nes->ram = ram;
    nes->vram = vram;
    nes->oam = oam;
    nes->scanout_buf = scanout_buf;

    return nes;

err_cleanup_5:
    free(oam);
err_cleanup_4:
    free(scanout_buf);
err_cleanup_3:
    free(vram);
err_cleanup_2:
    free(ram);
err_cleanup_1:
    return nullptr;
}

struct nes_t *nes_create()
{
    return nes_init(calloc(1, sizeof(struct nes_t)));
}

void nes_deinit(struct nes_t *nes)
{
    if (nes == nullptr)
        return;

    if (nes->mapper_init)
        mapper_deinit(nes->mapper);

    free(nes->oam);
    free(nes->scanout_buf);
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

    printf("Mapper number: $%02X\n", ines->mapper_num);
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

static void nes_write_mem_v(void *ptr, u16 addr, u8 value)
{
    nes_write_mem(ptr, addr, value);
}

static const struct memory_vtable_t NES_MEMORY_VTABLE = {
    .deinit = nes_deinit_v,
    .read = nes_read_mem_v,
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
    struct memory_t mem = nes_as_memory(nes);
    cpu_reset(&nes->cpu, mem);
}

void nes_set_btn(struct nes_t *nes, enum nes_btn_t btn, bool pressed)
{
    set_bits(&nes->btns, 1 << btn, pressed);
}

static constexpr size_t DOTS_X = 341;
static constexpr size_t DOTS_Y = 262;

static_assert(DOTS_X >= NES_SCREEN_WIDTH);
static_assert(DOTS_Y >= NES_SCREEN_HEIGHT);

static constexpr size_t LINE_PRE_RENDER = DOTS_Y - 1;
static constexpr size_t LINE_VBLANK = 241;

static void nes_update_scanout_bg(struct nes_t nes[static 1], size_t nt_addr,
                                  u8 col_mask)
{
    u16 pat_tbl_addr =
        (nes->ppuctrl & PPUCTRL_BG_PAT_ADDR) == 0 ? 0x0000 : 0x1000;

    for (size_t ty = 0; ty < 30; ++ty) {
        for (size_t tx = 0; tx < 32; ++tx) {
            size_t dst_x = 8 * tx;
            size_t dst_y = 8 * ty;

            u8 t_idx = nes_read_ppu(nes, nt_addr + (ty * 32) + tx);
            u16 pat_base = pat_tbl_addr + (16ULL * t_idx);

            size_t attr_idx = ((ty / 4) * 8) + (tx / 4);
            size_t attr_quad = (((ty / 2) % 2) * 2) + ((tx / 2) % 2);

            u8 attr = nes_read_ppu(nes, nt_addr + 0x3C0 + attr_idx);
            u8 pal = (attr >> (2 * attr_quad)) & 0b11;

            for (size_t py = 0; py < 8; ++py) {
                u8 p0 = nes_read_ppu(nes, pat_base + py);
                u8 p1 = nes_read_ppu(nes, pat_base + py + 8);

                for (size_t px = 0; px < 8; ++px) {
                    u8 b0 = p0 >> 7;
                    u8 b1 = p1 >> 7;
                    u8 entry = b0 | (b1 << 1);

                    if (entry != 0) {
                        u8 col = nes->pal_ram[(4 * pal) + entry];
                        nes->scanout_buf[dst_y + py][dst_x + px] =
                            col & col_mask;
                    }

                    p0 <<= 1;
                    p1 <<= 1;
                }
            }
        }
    }
}

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

static void nes_update_scanout_sprs(struct nes_t nes[static 1], u8 col_mask,
                                    enum spr_priority_t priority)
{
    u16 pt_addr = (nes->ppuctrl & PPUCTRL_SPR_PAT_ADDR) == 0 ? 0x0000 : 0x1000;

    for (size_t i = 0; i < 64; ++i) {
        u8 y_pos = nes->oam[(4 * i)] + 1;
        if (y_pos == 0)
            continue;

        u8 tile_idx = nes->oam[(4 * i) + 1];
        u8 attrs = nes->oam[(4 * i) + 2];
        u8 x_pos = nes->oam[(4 * i) + 3];

        enum spr_priority_t spr_priority =
            (attrs & ATTRS_PRIORITY) == 0 ? PRIORITY_LOW : PRIORITY_HIGH;

        if (spr_priority != priority)
            continue;

        u8 pal = attrs & ATTRS_PALETTE;
        bool flip_hor = (attrs & ATTRS_FLIP_HORIZONTAL) != 0;
        bool flip_ver = (attrs & ATTRS_FLIP_VERTICAL) != 0;

        u16 pat_addr = pt_addr + (16ULL * tile_idx);

        for (size_t py = 0; py < 8; ++py) {
            u8 p0 = nes_read_ppu(nes, pat_addr + py);
            u8 p1 = nes_read_ppu(nes, pat_addr + py + 8);

            for (size_t px = 0; px < 8; ++px) {
                u8 entry_b0 = p0 >> 7;
                u8 entry_b1 = p1 >> 7;
                u8 entry = entry_b0 | (entry_b1 << 1);

                size_t py_real = flip_ver ? (7 - py) : py;
                size_t px_real = flip_hor ? (7 - px) : px;

                if (entry != 0) {
                    u8 col = nes->pal_ram[(4 * (4 + pal)) + entry];
                    nes->scanout_buf[y_pos + py_real][x_pos + px_real] =
                        col & col_mask;
                }

                p0 <<= 1;
                p1 <<= 1;
            }
        }
    }
}

static void nes_update_scanout(struct nes_t nes[static 1])
{
    assert((nes->ppuctrl & PPUCTRL_SPR_SIZE) == 0 &&
           "TODO: implement 8x16 sprites");

    size_t nt_addr = 0x2000 + (0x400 * (nes->ppuctrl & PPUCTRL_BASE_NT_ADDR));

    u8 col_mask = (nes->ppumask & PPUMASK_GREYSCALE) == 0 ? 0xFF : 0x30;

    // entry 0 of palette 0 is used as backdrop color
    memset(nes->scanout_buf, nes->pal_ram[0] & col_mask,
           NES_SCREEN_HEIGHT * sizeof(nes->scanout_buf[0]));

    if ((nes->ppumask & PPUMASK_SPR_EN) != 0)
        nes_update_scanout_sprs(nes, col_mask, PRIORITY_HIGH);

    if ((nes->ppumask & PPUMASK_BG_EN) != 0)
        nes_update_scanout_bg(nes, nt_addr, col_mask);

    if ((nes->ppumask & PPUMASK_SPR_EN) != 0)
        nes_update_scanout_sprs(nes, col_mask, PRIORITY_LOW);
}

u64 nes_dispatch_cpu(struct nes_t *nes)
{
    if (nes->oamdma_state != DMA_STATE_IDLE)
        return CPU_CLK_RATIO; // cpu is halted during oam dma

    bool irq = nes->irq_apu_dmc || nes->irq_apu_fc;

    u64 start_cycles = nes->cpu.cyc;

    if (irq) {
        printf("irq\n");
        cpu_request_irq(&nes->cpu, nes_as_memory(nes));
    }

    cpu_step(&nes->cpu, nes_as_memory(nes));

    return CPU_CLK_RATIO * (nes->cpu.cyc - start_cycles);
}

u64 nes_dispatch_pixel(struct nes_t *nes)
{
    if (++nes->px == DOTS_X) {
        nes->px = 0;

        if (++nes->py == DOTS_Y)
            nes->py = 0;
    }

    if (nes->py == LINE_VBLANK && nes->px == 1) {
        // vblank
        set_bits(&nes->ppustatus, PPUSTATUS_VBLANK, true);

        if ((nes->ppuctrl & PPUCTRL_VBLANK_NMI_EN) != 0)
            cpu_request_nmi(&nes->cpu, nes_as_memory(nes));
    }

    if (nes->py == LINE_PRE_RENDER && nes->px == 1) {
        // pre-render
        printf("ppustatus: $%02X\n", nes->ppustatus);
        set_bits(&nes->ppustatus, PPUSTATUS_VBLANK, false);
        nes_update_scanout(nes);
    }

    return PPU_CLK_RATIO;
}

u64 nes_dispatch_dma_cycle(struct nes_t *nes)
{
    switch (nes->oamdma_state) {
        case DMA_STATE_IDLE:
            break;
        case DMA_STATE_READ:
            if (nes->dma_cycle == DMA_CYCLE_GET) {
                u16 src_addr = ((u16)nes->oamdma_page << 8) | nes->dma_counter;
                nes->dma_buf = nes_read_mem(nes, src_addr);
                nes->oamdma_state = DMA_STATE_WRITE;
            }
            break;
        case DMA_STATE_WRITE:
            assert(nes->dma_cycle == DMA_CYCLE_PUT);
            nes->oam[nes->dma_counter++] = nes->dma_buf;

            if (nes->dma_counter == 0)
                nes->oamdma_state = DMA_STATE_IDLE;
            else
                nes->oamdma_state = DMA_STATE_READ;
            break;
    }

    nes->dma_cycle ^= 1;
    return CPU_CLK_RATIO;
}

const u8 (*nes_get_scanout(const struct nes_t *nes))
    [NES_SCREEN_WIDTH] { return nes->scanout_buf; }
