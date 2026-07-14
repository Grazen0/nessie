#include "error.h"
#include "frontend/common.h"
#include "nes.h"
#include "scheduler.h"
#include "util.h"
#include <assert.h>
#include <getopt.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define EXE_NAME "nessie"

static constexpr char USAGE[] = "Usage: " EXE_NAME " [options] [rom_file] \n"
                                "A NES emulator written in C.             \n"
                                "                                         \n"
                                "  -h, --help   show this help message    \n";

static const struct option OPTIONS[] = {
    {"help", no_argument, nullptr, 'h'},
};

struct args_t {
    char *rom_file;
};

static bool parse_args(int argc, char *argv[static 1],
                       struct args_t out_args[static 1])
{
    struct args_t args = {
        .rom_file = nullptr,
    };

    int opt = -1;

    while ((opt = getopt_long(argc, argv, "hb:l:", OPTIONS, nullptr)) != -1) {
        switch (opt) {
            case 'h':
                fputs(USAGE, stdout);
                exit(0);
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

    struct nes_t *nes = nes_create();
    if (nes == nullptr) {
        fprintf(stderr, "Error initializing emulator.");
        retval = EXIT_FAILURE;
        goto cleanup_1;
    }

    enum nes_error_t err = NES_OK;

    struct ines_t ines = {};
    if ((err = ines_parse(rom_len, rom, &ines)) != NES_OK) {
        fprintf(stderr, "Error: %s\n", nes_error_str(err));
        retval = EXIT_FAILURE;
        goto cleanup_2;
    }

    if ((err = nes_load_rom(nes, &ines)) != NES_OK) {
        fprintf(stderr, "Error: %s\n", nes_error_str(err));
        retval = EXIT_FAILURE;
        goto cleanup_2;
    }

    nes_reset(nes);

    struct sched_t *sched = sched_create();
    assert(sched != nullptr);

    printf("Using frontend '%s'\n", frontend.name);
    frontend.run(nes, sched);

    sched_destroy(sched);
cleanup_2:
    nes_destroy(nes);
cleanup_1:
    free(rom);

    return retval;
}
