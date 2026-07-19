CC         = m68k-atari-mint-gcc

# -m68000          : strict 68000 instruction set (no 68020/030/FPU)
# -O1              : safe optimisation level, avoids 68020 idioms from -O2
# -fno-strict-aliasing : safe ob_spec union access via .index / .tedinfo
# -D__GEMLIB_OLDNAMES  : expose NONE, EXIT, SELECTED, etc. from mt_gem.h
CFLAGS     = -Wall -Wextra -O1 -m68000 -fno-strict-aliasing \
             -I include -D__GEMLIB_OLDNAMES

# -s : strip symbols from the final executable
LDFLAGS    = -s -lgem

TARGET     = SIDETNFS.PRG
SRCS       = src/main.c src/drive.c src/dialog.c src/sidetnfs_probe.c
INSTALLDIR = /mnt/retroloft/retro/Atari.ST/CONFIG

# Files to copy on 'make install' (extend as the project grows)
DISTFILES  = $(TARGET)

# 'all' always installs too: every successful build is copied straight
# to the CONFIG share, no separate 'make install' step needed.
all: $(TARGET) install

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) $(LDFLAGS)

install: $(DISTFILES)
	cp -v $(DISTFILES) $(INSTALLDIR)/

clean:
	rm -f $(TARGET)

.PHONY: all install clean
