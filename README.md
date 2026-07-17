# Nessie

A NES emulator written in C.

Supports only mappers 000 and 001 so far. PPU rendering is not as accurate as it should be for now, but we're getting there. No sound support yet.

The following games have been tested to be playable:

- Donkey Kong
- Super Mario Bros.
- Dr. Mario
- Ice Climber
- Tetris
- Metroid
- Mega Man 2

## Development

This project is configured with [Meson]. You will also need the following dependencies:

- [Raylib]
- pkg-config (probably)

You can then compile and install the project as follows:

```bash
meson setup build
meson compile -C build
meson install -C build
```

[meson]: https://mesonbuild.org
[raylib]: https://raylib.com
