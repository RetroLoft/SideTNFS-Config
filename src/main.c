#include <gem.h>
#include <mint/osbind.h>
#include "dialog.h"
#include "drive.h"

static DriveConfig cfg;

/* Same cold-reset sequence as dialog.c's atari_do_reset() (the post-save
 * "Reset Now" choice) -- duplicated here rather than shared across
 * modules for such a small, self-contained utility. Clears the three
 * "memory valid" checksum longs so TOS reinitializes fully, then jumps
 * through the reset vector at $4. Must run in supervisor mode (all
 * touched addresses are below the low-memory boundary TOS protects from
 * user-mode access) -- only ever called via Supexec(), never directly. */
static long atari_do_reset(void)
{
    *(volatile long *)0x420L = 0;
    *(volatile long *)0x43AL = 0;
    *(volatile long *)0x51AL = 0;
    ((void (*)(void))(*(volatile long *)0x4L))();
    return 0; /* unreached */
}

int main(void)
{
    appl_init();

    /* The widest dialogs (TNFS editor, WiFi/Network settings, drive
     * overview) are 50 character columns wide -- 400px at the standard
     * 8px system font, wider than the whole 320px low-res screen. That
     * is a genuine width overflow, not something the existing low-res
     * row-height shrink (see dialog.c) can fix.
     *
     * Runtime resolution switching without a reboot was tried and
     * reverted: on real single-tasking TOS the AES/VDI stay tied to
     * whatever resolution the desktop booted in, so a bare Setscreen()
     * from inside a running GEM app corrupts the palette and layout.
     * But Setscreen() *followed immediately by a reset* is exactly the
     * two-step sequence GEM's own "Set Preferences" resolution changer
     * uses internally: the shifter hardware register Setscreen() writes
     * is untouched by a CPU-level reset, so the machine comes back up
     * already in medium resolution and TOS/AES/desktop initialize fresh
     * against it. Offer that as a one-click fix instead of just failing.
     * No AES calls happen between Setscreen() and the reset (the
     * documented "don't touch AES after Setscreen()" constraint), and
     * the reboot itself replaces our own appl_exit()/cleanup. */
    if (Getrez() == 0) {
        /* Cancel is the default (button 1): rebooting the whole machine
         * is easy to trigger by accident otherwise. */
        if (form_alert(1, "[3][SideTNFS Config|This program requires|Medium or High resolution.]"
                          "[Cancel|Switch & Reboot]") == 2) {
            Setscreen(-1L, -1L, 1);
            Supexec(atari_do_reset);
        }
        appl_exit();
        return 0;
    }

    graf_mouse(ARROW, (MFORM *)0);

    /* In-memory starting point only (no file I/O) -- dialog_run()
     * overrides this from the cartridge firmware when present. */
    drive_config_init_defaults(&cfg);
    dialog_run(&cfg);

    appl_exit();
    return 0;
}
