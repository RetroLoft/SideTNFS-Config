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
SideTNFS firmware (protocol v3) on the cartridge; if found and consistent,
the configuration is read entirely from the firmware's RAM/flash config and
shown as-is. If no firmware is found (or its configuration is unusable),
the overview falls back to a built-in default (a SETTINGS disk plus a
RetroLoft TNFS drive) held only in memory for that session.

Save always writes straight to the Pico: RAM staging, then a single
`SAVE_CONFIG` flash write, then a full readback verification before
reporting success. Without firmware detected, Save has nowhere to write
to and reports a clear error instead.

## Drives

`SIDETNFS.PRG` configures SideTNFS.

- The **SETTINGS disk** is always present and read-only; its default
  drive letter is `S:`.
- Up to eight ordinary TNFS/SD drives can be configured, each
  **Active** or **Inactive**. Setting a drive to Inactive keeps its full
  configuration -- it is simply not published as a GEMDOS drive.
- **Remove** clears a drive's slot completely; you will need to re-enter
  its details if you want it back.
- Changes only take effect after **Save**, and only become active once
  the Sidecartridge itself restarts.
