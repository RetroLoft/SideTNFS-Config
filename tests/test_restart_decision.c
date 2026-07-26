/*
 * Host-side logic test for the Fase 9B restart decision
 * (configuration_changes_require_restart(), dialog.c).
 *
 * Like the other tests/ files, this mirrors a single *static* dialog.c
 * function verbatim (documented as a mirror, same convention as
 * test_netconfig_protocol.c/test_drive_slots.c) rather than linking it
 * directly, since dialog.c pulls in <gem.h> and real ROM3 hardware
 * access that don't exist on the host.
 *
 * Build/run:
 *   cc -Wall -Wextra -std=c89 -pedantic -o /tmp/test_restart_decision \
 *       tests/test_restart_decision.c && /tmp/test_restart_decision
 *
 * NOT host-testable here (needs real SideTNFS hardware/GEM): the actual
 * Restart/Later dialog, sidetnfs_probe_reboot_pico()'s ACK/timeout
 * handling, and the Atari-reset timing itself -- see the report for the
 * full list of scenarios (F-L) that require hardware.
 */

#include <stdio.h>

static int g_failures = 0;

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
            g_failures++; \
        } \
    } while (0)

/* Verbatim mirror of dialog.c's configuration_changes_require_restart(). */
static int configuration_changes_require_restart(int drives_changed, int network_changed, int rtc_changed)
{
    return drives_changed || network_changed || rtc_changed;
}

/* Test A: SAVE with nothing changed anywhere -- no restart. */
static void test_a_no_changes(void)
{
    CHECK(!configuration_changes_require_restart(0, 0, 0), "A: nothing changed -> no restart");
}

/* Test C: only drives changed (e.g. Active -> Inactive). */
static void test_c_drives_only(void)
{
    CHECK(configuration_changes_require_restart(1, 0, 0), "C: drives changed -> restart required");
}

/* Test D/E: TNFS host or any other drive field changed -- same
 * block-level flag as C, no finer granularity exists. */
static void test_d_e_drive_field_changed(void)
{
    CHECK(configuration_changes_require_restart(1, 0, 0), "D/E: any drive field change -> restart required");
}

/* Test E (WiFi variant): network-only change. */
static void test_network_only(void)
{
    CHECK(configuration_changes_require_restart(0, 1, 0), "network only -> restart required");
}

static void test_rtc_only(void)
{
    CHECK(configuration_changes_require_restart(0, 0, 1), "RTC only -> restart required");
}

/* Test K: multiple changed blocks still yield exactly one restart
 * decision (true), not a count -- the caller (offer_restart_if_needed())
 * only ever sends REBOOT_PICO once regardless of how many blocks
 * changed. */
static void test_k_multiple_blocks_changed(void)
{
    CHECK(configuration_changes_require_restart(1, 1, 0), "K: drives+network changed -> restart required (once)");
    CHECK(configuration_changes_require_restart(1, 1, 1), "K: all three changed -> restart required (once)");
}

int main(void)
{
    test_a_no_changes();
    test_c_drives_only();
    test_d_e_drive_field_changed();
    test_network_only();
    test_rtc_only();
    test_k_multiple_blocks_changed();

    if (g_failures == 0) {
        printf("All host-side restart-decision tests passed.\n");
        return 0;
    }
    printf("%d test(s) FAILED.\n", g_failures);
    return 1;
}
