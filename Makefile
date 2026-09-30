CC         = m68k-atari-mint-gcc

# -m68000          : strict 68000 instruction set (no 68020/030/FPU)
# -O1              : safe optimisation level, avoids 68020 idioms from -O2
# -fno-strict-aliasing : safe ob_spec union access via .index / .tedinfo
# -D__GEMLIB_OLDNAMES  : expose NONE, EXIT, SELECTED, etc. from mt_gem.h
CFLAGS     = -Wall -Wextra -O1 -m68000 -fno-strict-aliasing \
             -I include -D__GEMLIB_OLDNAMES

# -s : strip symbols from the final executable
LDFLAGS    = -s -lgem

TARGET      = SIDETNFS.PRG
TARGET_DEMO = DEMOTNFS.PRG
TARGET_ACSI = ACSITNFS.PRG
SRCS        = src/main.c src/drive.c src/netconfig.c src/rtcconfig.c src/dialog.c src/sidetnfs_probe.c
INSTALLDIR  = /mnt/retroloft/retro/Atari.ST/CONFIG

# Files to copy on 'make install' (extend as the project grows).
# README.TXT ships on the SETTINGS disk alongside SIDETNFS.PRG and MUST
# be included as a release asset whenever a release is published -- see
# README.md.
DISTFILES  = $(TARGET) README.TXT

# 'all' always installs too: every successful build is copied straight
# to the CONFIG share, no separate 'make install' step needed.
all: $(TARGET) install

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) $(LDFLAGS)

# Demo build: DEMO_MODE guards every cartridge-communication call site in
# dialog.c so DEMOTNFS.PRG never touches ROM3 at all -- for recording a
# demo video in Hatari with no SideTNFS cartridge/Pico present. Unlike
# 'all', this never installs: DEMOTNFS.PRG stays local to the project dir.
demo: $(SRCS)
	$(CC) $(CFLAGS) -DDEMO_MODE -o $(TARGET_DEMO) $(SRCS) $(LDFLAGS)

# ACSI build: the same UI, but the configuration protocol goes over the
# ACSI/DMA port to an ACSI2TNFS adapter (src/sidetnfs_acsi.c replaces the
# cartridge transport src/sidetnfs_probe.c). Never installs.
ACSI_SRCS = $(filter-out src/sidetnfs_probe.c,$(SRCS)) src/sidetnfs_acsi.c

acsi: $(TARGET_ACSI)

$(TARGET_ACSI): $(ACSI_SRCS)
	$(CC) $(CFLAGS) -DTRANSPORT_ACSI -o $(TARGET_ACSI) $(ACSI_SRCS) $(LDFLAGS)

install: $(DISTFILES)
	cp -v $(DISTFILES) $(INSTALLDIR)/

clean:
	rm -f $(TARGET) $(TARGET_DEMO) $(TARGET_ACSI)

.PHONY: all install clean demo acsi
