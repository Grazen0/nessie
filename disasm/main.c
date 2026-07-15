#include <assert.h>
#include <getopt.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

typedef int64_t i64;
typedef int32_t i32;
typedef int16_t i16;
typedef int8_t i8;

static const struct option OPTIONS[] = {
    {"help", no_argument, nullptr, 'h'},
};

struct args_t {
    char *rom_file;
    bool help;
};

static struct args_t args_init()
{
    return (struct args_t){
        .help = false,
    };
}

static bool parse_args(int argc, char *argv[static 1],
                       struct args_t out_args[static 1])
{
    *out_args = args_init();

    int opt = -1;

    while ((opt = getopt_long(argc, argv, "h", OPTIONS, nullptr)) != -1) {
        switch (opt) {
            case 'h':
                out_args->help = true;
                return true;
            default:
                return false;
        }
    }

    if (optind >= argc)
        return false;

    out_args->rom_file = argv[optind];
    return true;
}

#define EXE_NAME "disasm"

static constexpr char USAGE[] = "Usage: " EXE_NAME " [options] [rom_file] \n"
                                "NES disassembler.                    \n"
                                "                                     \n"
                                "  -h, --help   show this help message\n";

static size_t fsize(FILE *file)
{

    long pos = ftell(file);
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, pos, SEEK_SET);
    return size;
}

struct operand_t {
    enum : u8 {
        OP_UNKNOWN,
        OP_A,
        OP_ABS,
        OP_ABS_X,
        OP_ABS_Y,
        OP_IMM,
        OP_IMPL,
        OP_IND,
        OP_X_IND,
        OP_IND_Y,
        OP_REL,
        OP_ZPG,
        OP_ZPG_X,
        OP_ZPG_Y,
    } kind;
    union {
        u8 val_u8;
        u16 val_u16;
    };
};

static void print_op(struct operand_t op, FILE *out)
{
    switch (op.kind) {
        case OP_A:
            fprintf(out, "a");
            break;
        case OP_ABS:
            fprintf(out, "$%04X", op.val_u16);
            break;
        case OP_ABS_X:
            fprintf(out, "$%04X, x", op.val_u16);
            break;
        case OP_ABS_Y:
            fprintf(out, "$%04X, y", op.val_u16);
            break;
        case OP_IMM:
            fprintf(out, "#$%02X", op.val_u8);
            break;
        case OP_UNKNOWN:
        case OP_IMPL:
            break;
        case OP_IND:
            fprintf(out, "($%04X)", op.val_u16);
            break;
        case OP_X_IND:
            fprintf(out, "($%02X, x)", op.val_u8);
            break;
        case OP_IND_Y:
            fprintf(out, "($%02X), y", op.val_u8);
            break;
        case OP_REL:
            fprintf(out, "%d", (i8)op.val_u8);
            break;
        case OP_ZPG:
            fprintf(out, "$%02X", op.val_u8);
            break;
        case OP_ZPG_X:
            fprintf(out, "$%02X, x", op.val_u8);
            break;
        case OP_ZPG_Y:
            fprintf(out, "$%02X, y", op.val_u8);
            break;
    }
}

struct instr_t {
    enum : u8 {
        INSTR_UNKNOWN,

        INSTR_ORA,
        INSTR_AND,
        INSTR_EOR,
        INSTR_ADC,
        INSTR_STA,
        INSTR_LDA,
        INSTR_CMP,
        INSTR_SBC,

        INSTR_ASL,
        INSTR_ROL,
        INSTR_LSR,
        INSTR_ROR,
        INSTR_STX,
        INSTR_LDX,
        INSTR_DEC,
        INSTR_INC,

        INSTR_BIT,
        INSTR_JMP,
        INSTR_STY,
        INSTR_LDY,
        INSTR_CPY,
        INSTR_CPX,

        INSTR_BPL,
        INSTR_BMI,
        INSTR_BVC,
        INSTR_BVS,
        INSTR_BCC,
        INSTR_BCS,
        INSTR_BNE,
        INSTR_BEQ,

        INSTR_BRK,
        INSTR_JSR,
        INSTR_RTI,
        INSTR_RTS,

        INSTR_PHP,
        INSTR_PLP,
        INSTR_PHA,
        INSTR_PLA,
        INSTR_DEY,
        INSTR_TAY,
        INSTR_INY,
        INSTR_INX,

        INSTR_CLC,
        INSTR_SEC,
        INSTR_CLI,
        INSTR_SEI,
        INSTR_TYA,
        INSTR_CLV,
        INSTR_CLD,
        INSTR_SED,

        INSTR_TXA,
        INSTR_TXS,
        INSTR_TAX,
        INSTR_TSX,
        INSTR_DEX,
        INSTR_NOP,
    } kind;
    struct operand_t op;
};

static void print_instr(struct instr_t instr, FILE *out)
{
    static const char *NAMES[] = {
        [INSTR_UNKNOWN] = "?",

        [INSTR_ORA] = "ora",   [INSTR_AND] = "and", [INSTR_EOR] = "eor",
        [INSTR_ADC] = "adc",   [INSTR_STA] = "sta", [INSTR_LDA] = "lda",
        [INSTR_CMP] = "cmp",   [INSTR_SBC] = "sbc",

        [INSTR_ASL] = "asl",   [INSTR_ROL] = "rol", [INSTR_LSR] = "lsr",
        [INSTR_ROR] = "ror",   [INSTR_STX] = "stx", [INSTR_LDX] = "ldx",
        [INSTR_DEC] = "dec",   [INSTR_INC] = "inc", [INSTR_BIT] = "bit",
        [INSTR_JMP] = "jmp",   [INSTR_STY] = "sty", [INSTR_LDY] = "ldy",
        [INSTR_CPY] = "cpy",   [INSTR_CPX] = "cpx",

        [INSTR_BPL] = "bpl",   [INSTR_BMI] = "bmi", [INSTR_BVC] = "bvc",
        [INSTR_BVS] = "bvs",   [INSTR_BCC] = "bcc", [INSTR_BCS] = "bcs",
        [INSTR_BNE] = "bne",   [INSTR_BEQ] = "beq", [INSTR_BRK] = "brk",
        [INSTR_JSR] = "jsr",   [INSTR_RTI] = "rti", [INSTR_RTS] = "rts",

        [INSTR_PHP] = "php",   [INSTR_PLP] = "plp", [INSTR_PHA] = "pha",
        [INSTR_PLA] = "pla",   [INSTR_DEY] = "dey", [INSTR_TAY] = "tay",
        [INSTR_INY] = "iny",   [INSTR_INX] = "inx",

        [INSTR_CLC] = "clc",   [INSTR_SEC] = "sec", [INSTR_CLI] = "cli",
        [INSTR_SEI] = "sei",   [INSTR_TYA] = "tya", [INSTR_CLV] = "clv",
        [INSTR_CLD] = "cld",   [INSTR_SED] = "sed",

        [INSTR_TXA] = "txa",   [INSTR_TXS] = "txs", [INSTR_TAX] = "tax",
        [INSTR_TSX] = "tsx",   [INSTR_DEX] = "dex", [INSTR_NOP] = "nop",
    };

    fprintf(out, "%s", NAMES[instr.kind]);

    if (instr.op.kind != OP_IMPL && instr.op.kind != OP_UNKNOWN) {
        fprintf(out, "     ");
        print_op(instr.op, out);
    }
}

enum flag_6_t : u8 {
    FLAG6_TRAINER = 1 << 2,
};

static u16 read_data_u16(const u8 data[static 1], u16 addr[static 1])
{
    u16 lo = data[(*addr)++];
    u16 hi = data[(*addr)++];
    return (hi << 8) | lo;
}

static struct instr_t parse_instr(const u8 data[static 1], u16 addr[static 1])
{
    u8 opcode = data[(*addr)++];

    static const struct instr_t MAPPING[256] = {
        [0x01] = {INSTR_ORA, {OP_X_IND, {0}}},
        [0x05] = {INSTR_ORA,   {OP_ZPG, {0}}},
        [0x09] = {INSTR_ORA,   {OP_IMM, {0}}},
        [0x0D] = {INSTR_ORA,   {OP_ABS, {0}}},
        [0x11] = {INSTR_ORA, {OP_IND_Y, {0}}},
        [0x15] = {INSTR_ORA, {OP_ZPG_X, {0}}},
        [0x19] = {INSTR_ORA, {OP_ABS_Y, {0}}},
        [0x1D] = {INSTR_ORA, {OP_ABS_X, {0}}},

        [0x21] = {INSTR_AND, {OP_X_IND, {0}}},
        [0x25] = {INSTR_AND,   {OP_ZPG, {0}}},
        [0x29] = {INSTR_AND,   {OP_IMM, {0}}},
        [0x2D] = {INSTR_AND,   {OP_ABS, {0}}},
        [0x31] = {INSTR_AND, {OP_IND_Y, {0}}},
        [0x35] = {INSTR_AND, {OP_ZPG_X, {0}}},
        [0x39] = {INSTR_AND, {OP_ABS_Y, {0}}},
        [0x3D] = {INSTR_AND, {OP_ABS_X, {0}}},

        [0x41] = {INSTR_EOR, {OP_X_IND, {0}}},
        [0x45] = {INSTR_EOR,   {OP_ZPG, {0}}},
        [0x49] = {INSTR_EOR,   {OP_IMM, {0}}},
        [0x4D] = {INSTR_EOR,   {OP_ABS, {0}}},
        [0x51] = {INSTR_EOR, {OP_IND_Y, {0}}},
        [0x55] = {INSTR_EOR, {OP_ZPG_X, {0}}},
        [0x59] = {INSTR_EOR, {OP_ABS_Y, {0}}},
        [0x5D] = {INSTR_EOR, {OP_ABS_X, {0}}},

        [0x61] = {INSTR_ADC, {OP_X_IND, {0}}},
        [0x65] = {INSTR_ADC,   {OP_ZPG, {0}}},
        [0x69] = {INSTR_ADC,   {OP_IMM, {0}}},
        [0x6D] = {INSTR_ADC,   {OP_ABS, {0}}},
        [0x71] = {INSTR_ADC, {OP_IND_Y, {0}}},
        [0x75] = {INSTR_ADC, {OP_ZPG_X, {0}}},
        [0x79] = {INSTR_ADC, {OP_ABS_Y, {0}}},
        [0x7D] = {INSTR_ADC, {OP_ABS_X, {0}}},

        [0x81] = {INSTR_STA, {OP_X_IND, {0}}},
        [0x85] = {INSTR_STA,   {OP_ZPG, {0}}},
        [0x8D] = {INSTR_STA,   {OP_ABS, {0}}},
        [0x91] = {INSTR_STA, {OP_IND_Y, {0}}},
        [0x95] = {INSTR_STA, {OP_ZPG_X, {0}}},
        [0x99] = {INSTR_STA, {OP_ABS_Y, {0}}},
        [0x9D] = {INSTR_STA, {OP_ABS_X, {0}}},

        [0xA1] = {INSTR_LDA, {OP_X_IND, {0}}},
        [0xA5] = {INSTR_LDA,   {OP_ZPG, {0}}},
        [0xA9] = {INSTR_LDA,   {OP_IMM, {0}}},
        [0xAD] = {INSTR_LDA,   {OP_ABS, {0}}},
        [0xB1] = {INSTR_LDA, {OP_IND_Y, {0}}},
        [0xB5] = {INSTR_LDA, {OP_ZPG_X, {0}}},
        [0xB9] = {INSTR_LDA, {OP_ABS_Y, {0}}},
        [0xBD] = {INSTR_LDA, {OP_ABS_X, {0}}},

        [0xC1] = {INSTR_CMP, {OP_X_IND, {0}}},
        [0xC5] = {INSTR_CMP,   {OP_ZPG, {0}}},
        [0xC9] = {INSTR_CMP,   {OP_IMM, {0}}},
        [0xCD] = {INSTR_CMP,   {OP_ABS, {0}}},
        [0xD1] = {INSTR_CMP, {OP_IND_Y, {0}}},
        [0xD5] = {INSTR_CMP, {OP_ZPG_X, {0}}},
        [0xD9] = {INSTR_CMP, {OP_ABS_Y, {0}}},
        [0xDD] = {INSTR_CMP, {OP_ABS_X, {0}}},

        [0xE1] = {INSTR_SBC, {OP_X_IND, {0}}},
        [0xE5] = {INSTR_SBC,   {OP_ZPG, {0}}},
        [0xE9] = {INSTR_SBC,   {OP_IMM, {0}}},
        [0xED] = {INSTR_SBC,   {OP_ABS, {0}}},
        [0xF1] = {INSTR_SBC, {OP_IND_Y, {0}}},
        [0xF5] = {INSTR_SBC, {OP_ZPG_X, {0}}},
        [0xF9] = {INSTR_SBC, {OP_ABS_Y, {0}}},
        [0xFD] = {INSTR_SBC, {OP_ABS_X, {0}}},

        [0x06] = {INSTR_ASL,   {OP_ZPG, {0}}},
        [0x0A] = {INSTR_ASL,     {OP_A, {0}}},
        [0x0E] = {INSTR_ASL,   {OP_ABS, {0}}},
        [0x16] = {INSTR_ASL, {OP_ZPG_X, {0}}},
        [0x1E] = {INSTR_ASL, {OP_ABS_X, {0}}},

        [0x26] = {INSTR_ROL,   {OP_ZPG, {0}}},
        [0x2A] = {INSTR_ROL,     {OP_A, {0}}},
        [0x2E] = {INSTR_ROL,   {OP_ABS, {0}}},
        [0x36] = {INSTR_ROL, {OP_ZPG_X, {0}}},
        [0x3E] = {INSTR_ROL, {OP_ABS_X, {0}}},

        [0x46] = {INSTR_LSR,   {OP_ZPG, {0}}},
        [0x4A] = {INSTR_LSR,     {OP_A, {0}}},
        [0x4E] = {INSTR_LSR,   {OP_ABS, {0}}},
        [0x56] = {INSTR_LSR, {OP_ZPG_X, {0}}},
        [0x5E] = {INSTR_LSR, {OP_ABS_X, {0}}},

        [0x66] = {INSTR_ROR,   {OP_ZPG, {0}}},
        [0x6A] = {INSTR_ROR,     {OP_A, {0}}},
        [0x6E] = {INSTR_ROR,   {OP_ABS, {0}}},
        [0x76] = {INSTR_ROR, {OP_ZPG_X, {0}}},
        [0x7E] = {INSTR_ROR, {OP_ABS_X, {0}}},

        [0x86] = {INSTR_STX,   {OP_ZPG, {0}}},
        [0x8E] = {INSTR_STX,   {OP_ABS, {0}}},
        [0x96] = {INSTR_STX, {OP_ZPG_Y, {0}}},

        [0xA2] = {INSTR_LDX,   {OP_IMM, {0}}},
        [0xA6] = {INSTR_LDX,   {OP_ZPG, {0}}},
        [0xAE] = {INSTR_LDX,   {OP_ABS, {0}}},
        [0xB6] = {INSTR_LDX, {OP_ZPG_Y, {0}}},
        [0xBE] = {INSTR_LDX, {OP_ABS_Y, {0}}},

        [0x84] = {INSTR_STY,   {OP_ZPG, {0}}},
        [0x8C] = {INSTR_STY,   {OP_ABS, {0}}},
        [0x94] = {INSTR_STY, {OP_ZPG_X, {0}}},

        [0xA0] = {INSTR_LDY,   {OP_IMM, {0}}},
        [0xA4] = {INSTR_LDY,   {OP_ZPG, {0}}},
        [0xAC] = {INSTR_LDY,   {OP_ABS, {0}}},
        [0xB4] = {INSTR_LDY, {OP_ZPG_X, {0}}},
        [0xBC] = {INSTR_LDY, {OP_ABS_X, {0}}},

        [0xC6] = {INSTR_DEC,   {OP_ZPG, {0}}},
        [0xCE] = {INSTR_DEC,   {OP_ABS, {0}}},
        [0xD6] = {INSTR_DEC, {OP_ZPG_X, {0}}},
        [0xDE] = {INSTR_DEC, {OP_ABS_X, {0}}},

        [0xE6] = {INSTR_INC,   {OP_ZPG, {0}}},
        [0xEE] = {INSTR_INC,   {OP_ABS, {0}}},
        [0xF6] = {INSTR_INC, {OP_ZPG_X, {0}}},
        [0xFE] = {INSTR_INC, {OP_ABS_X, {0}}},

        [0x00] = {INSTR_BRK,  {OP_IMPL, {0}}},
        [0x20] = {INSTR_JSR,   {OP_ABS, {0}}},
        [0x40] = {INSTR_RTI,  {OP_IMPL, {0}}},
        [0x60] = {INSTR_RTS,  {OP_IMPL, {0}}},

        [0x8A] = {INSTR_TXA,  {OP_IMPL, {0}}},
        [0x9A] = {INSTR_TXS,  {OP_IMPL, {0}}},
        [0xAA] = {INSTR_TAX,  {OP_IMPL, {0}}},
        [0xBA] = {INSTR_TSX,  {OP_IMPL, {0}}},
        [0xCA] = {INSTR_DEX,  {OP_IMPL, {0}}},
        [0xEA] = {INSTR_NOP,  {OP_IMPL, {0}}},

        [0x10] = {INSTR_BPL,   {OP_REL, {0}}},
        [0x30] = {INSTR_BMI,   {OP_REL, {0}}},
        [0x50] = {INSTR_BVC,   {OP_REL, {0}}},
        [0x70] = {INSTR_BVS,   {OP_REL, {0}}},
        [0x90] = {INSTR_BCC,   {OP_REL, {0}}},
        [0xB0] = {INSTR_BCS,   {OP_REL, {0}}},
        [0xD0] = {INSTR_BNE,   {OP_REL, {0}}},
        [0xF0] = {INSTR_BEQ,   {OP_REL, {0}}},

        [0x08] = {INSTR_PHP,  {OP_IMPL, {0}}},
        [0x18] = {INSTR_CLC,  {OP_IMPL, {0}}},
        [0x28] = {INSTR_PLP,  {OP_IMPL, {0}}},
        [0x38] = {INSTR_SEC,  {OP_IMPL, {0}}},
        [0x48] = {INSTR_PHA,  {OP_IMPL, {0}}},
        [0x58] = {INSTR_CLI,  {OP_IMPL, {0}}},
        [0x68] = {INSTR_PLA,  {OP_IMPL, {0}}},
        [0x78] = {INSTR_SEI,  {OP_IMPL, {0}}},
        [0x88] = {INSTR_DEY,  {OP_IMPL, {0}}},
        [0x98] = {INSTR_TYA,  {OP_IMPL, {0}}},
        [0xA8] = {INSTR_TAY,  {OP_IMPL, {0}}},
        [0xB8] = {INSTR_CLV,  {OP_IMPL, {0}}},
        [0xc8] = {INSTR_INY,  {OP_IMPL, {0}}},
        [0xD8] = {INSTR_CLD,  {OP_IMPL, {0}}},
        [0xE8] = {INSTR_INX,  {OP_IMPL, {0}}},
        [0xF8] = {INSTR_SED,  {OP_IMPL, {0}}},

        [0xC0] = {INSTR_CPY,   {OP_IMM, {0}}},
        [0xC4] = {INSTR_CPY,   {OP_ZPG, {0}}},
        [0xCC] = {INSTR_CPY,   {OP_ABS, {0}}},

        [0xE0] = {INSTR_CPX,   {OP_IMM, {0}}},
        [0xE4] = {INSTR_CPX,   {OP_ZPG, {0}}},
        [0xEC] = {INSTR_CPX,   {OP_ABS, {0}}},

        [0x24] = {INSTR_BIT,   {OP_ZPG, {0}}},
        [0x2C] = {INSTR_BIT,   {OP_ABS, {0}}},
        [0x4C] = {INSTR_JMP,   {OP_ABS, {0}}},
        [0x6C] = {INSTR_JMP,   {OP_IND, {0}}},
    };

    struct instr_t instr = MAPPING[opcode];

    if (instr.kind != INSTR_UNKNOWN) {
        switch (instr.op.kind) {
            case OP_UNKNOWN:
            case OP_A:
            case OP_IMPL:
                break;
            case OP_ABS:
            case OP_ABS_X:
            case OP_ABS_Y:
            case OP_IND:
                instr.op.val_u16 = read_data_u16(data, addr);
                break;
            case OP_IMM:
            case OP_X_IND:
            case OP_IND_Y:
            case OP_REL:
            case OP_ZPG:
            case OP_ZPG_X:
            case OP_ZPG_Y:
                instr.op.val_u8 = data[(*addr)++];
                break;
        }
    }

    return instr;
}

static void disassemble(size_t n, const u8 data[static n], u16 load_addr,
                        FILE *out)
{
    static constexpr size_t MAX_INSTR_BYTES = 3;

    u16 off = 0;

    while (off < n) {
        fprintf(out, "%04X    ", load_addr + off);

        u16 off_start = off;
        struct instr_t instr = parse_instr(data, &off);
        u16 off_end = off;

        for (size_t i = 0; i < MAX_INSTR_BYTES; ++i) {
            if (off_start + i < off_end)
                fprintf(out, "%02X ", data[off_start + i]);
            else
                fprintf(out, "   ");
        }

        fprintf(out, "  ");
        print_instr(instr, out);
        fprintf(out, "\n");
    }
}

int main(int argc, char *argv[static argc + 1])
{
    struct args_t args = args_init();

    if (!parse_args(argc, argv, &args)) {
        fputs(USAGE, stderr);
        printf("Try '" EXE_NAME " -h' for more information.\n");
        return EXIT_FAILURE;
    }

    if (args.help) {
        fputs(USAGE, stdout);
        return EXIT_SUCCESS;
    }

    FILE *file = fopen(args.rom_file, "r");

    if (file == nullptr) {
        perror("Could not open file");
        return EXIT_FAILURE;
    }

    int retval = EXIT_SUCCESS;

    size_t rom_len = fsize(file);

    static constexpr size_t HEADER_LEN = 16;

    if (rom_len < HEADER_LEN) {
        fprintf(stderr, "Not a ROM file.\n");
        retval = EXIT_FAILURE;
        goto cleanup_1;
    }

    u8 *rom = calloc(rom_len, sizeof(*rom));
    assert(fread(rom, sizeof(*rom), rom_len, file) == rom_len);

    if (rom[0] != 'N' || rom[1] != 'E' || rom[2] != 'S' || rom[3] != 0x1A) {
        fprintf(stderr, "Invalid magic number\n");
        retval = EXIT_FAILURE;
        goto cleanup_2;
    }

    size_t prg_rom_banks = rom[4];
    size_t chr_rom_banks = rom[5];

    printf("PRG ROM size (banks): %zu\n", prg_rom_banks);
    printf("CHR ROM size (banks): %zu\n", chr_rom_banks);
    printf("\n");

    static constexpr size_t BANK_SIZE = 0x4000;

    u16 base = 16;
    if ((rom[6] & FLAG6_TRAINER) != 0)
        base += 512; // size of trainer

    u8 *prg_rom = &rom[base];
    size_t prg_rom_len = BANK_SIZE * prg_rom_banks;

    u16 prg_load_addr = prg_rom_banks == 1 ? 0xC000 : 0x8000;
    disassemble(prg_rom_len, prg_rom, prg_load_addr, stdout);

cleanup_2:
    free(rom);
cleanup_1:
    fclose(file);
    return retval;
}
