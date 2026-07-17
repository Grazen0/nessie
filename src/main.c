#include "error.h"
#include "frontend/common.h"
#include "nes.h"
#include "scheduler.h"
#include "util.h"
#include <getopt.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define EXE_NAME "nessie"

static constexpr char USAGE[] =
    "Usage: " EXE_NAME " [options] [rom_file]      \n"
    "A NES emulator written in C.                  \n"
    "                                              \n"
    "  -h, --help           show this help message \n"
    "      --pc=ADDR        initial pc             \n"
    "      --log=FILE       output logs to file    \n";

static const struct option OPTIONS[] = {
    {"help", no_argument, nullptr, 'h'},
    {"pc", required_argument, nullptr, 'p'},
    {"log", required_argument, nullptr, 'l'},
    {},
};

struct args_t {
    char *rom_file;
    char *log_file;
    u16 init_pc;
    bool init_pc_init;
};

static bool parse_args(int argc, char *argv[static 1],
                       struct args_t out_args[static 1])
{
    struct args_t args = {};
    int opt = -1;

    while ((opt = getopt_long(argc, argv, "hl:", OPTIONS, nullptr)) != -1) {
        switch (opt) {
            case 'h':
                fputs(USAGE, stdout);
                exit(0);
            case 'p':
                args.init_pc = strtoul(optarg, nullptr, 0);
                args.init_pc_init = true;
                break;
            case 'l':
                args.log_file = optarg;
                break;
            default:
                return false;
        }
    }

    if (optind >= argc)
        return false;

    args.rom_file = argv[optind];

    *out_args = args;
    return true;
}

static u8 *load_file(const char filename[static 1], size_t *out_size)
{
    u8 *data = nullptr;
    FILE *file = fopen(filename, "r");

    if (file != nullptr) {
        fseek(file, 0, SEEK_END);
        size_t size = ftell(file);
        fseek(file, SEEK_SET, 0);

        data = malloc(size);

        if (data != nullptr)
            *out_size = fread(data, 1, size, file);

        fclose(file);
    }

    return data;
}

int main(int argc, char *argv[static argc + 1])
{
    struct args_t args = {};

    if (!parse_args(argc, argv, &args)) {
        fputs(USAGE, stderr);
        printf("Try '" EXE_NAME " -h' for more information.\n");
        return EXIT_FAILURE;
    }

    size_t rom_len = 0;
    u8 *rom = load_file(args.rom_file, &rom_len);
    if (rom == nullptr) {
        perror("Error opening ROM file");
        return EXIT_FAILURE;
    }

    int retval = EXIT_SUCCESS;

    FILE *log_file = nullptr;

    if (args.log_file) {
        printf("Outputting logs to '%s'\n", args.log_file);
        log_file = fopen(args.log_file, "w");

        if (log_file == nullptr) {
            perror("Error opening log file");
            retval = EXIT_FAILURE;
            goto cleanup_1;
        }
    }

    struct nes_t *nes = nes_create(log_file);
    if (nes == nullptr) {
        fprintf(stderr, "Error initializing emulator.");
        retval = EXIT_FAILURE;
        goto cleanup_2;
    }

    enum nes_error_t err = NES_OK;

    struct ines_t ines = {};
    if ((err = ines_parse(rom_len, rom, &ines)) != NES_OK) {
        fprintf(stderr, "Error: %s\n", nes_error_str(err));
        retval = EXIT_FAILURE;
        goto cleanup_3;
    }

    if ((err = nes_load_rom(nes, &ines)) != NES_OK) {
        fprintf(stderr, "Error: %s\n", nes_error_str(err));
        retval = EXIT_FAILURE;
        goto cleanup_3;
    }

    nes_reset(nes);

    if (args.init_pc_init) {
        printf("Using initial PC $%04X\n", args.init_pc);
        nes_set_pc(nes, args.init_pc);
    }

    struct sched_t *sched = sched_create(nes);
    ASSERT(sched != nullptr);

    printf("Using frontend '%s'\n", frontend.name);
    printf("\n");
    retval = frontend.run(nes, sched);

    sched_destroy(sched);
cleanup_3:
    nes_destroy(nes);
cleanup_2:
    free(rom);
cleanup_1:
    if (log_file != nullptr)
        fclose(log_file);

    return retval;
}
