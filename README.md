# SideTNFS Configuration Tool

Atari ST GEM configuration tool for SideTNFS.

## Requirements

- `m68k-atari-mint-gcc` cross-compiler
- `gemlib-m68k-atari-mint`
- `mintlib-m68k-atari-mint`

## Build

```
make
```

Output: `SIDETNFS.PRG`

## Clean

```
make clean
```

## Run

Transfer `SIDETNFS.PRG` to an Atari ST (or emulator such as Hatari) and run from the GEM desktop.

## Configuration source of truth

There is no local config file. On startup the tool tries to detect
SideTNFS firmware (protocol v2) on the cartridge; if found and consistent,
the drive list is read entirely from the firmware's RAM/flash config and
shown as-is. If no firmware is found (or its configuration is unusable),
the overview falls back to two built-in default rows (config disk, and a
RetroLoft TNFS drive) held only in memory for that session.

Save always writes straight to the Pico: RAM staging, then a single
`SAVE_CONFIG` flash write, then a full readback verification before
reporting success. Without firmware detected, Save has nowhere to write
to and reports a clear error instead.
