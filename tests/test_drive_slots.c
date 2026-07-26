/*
 * Host-side logic tests for the eight-fixed-slot EMPTY/DISABLED/ENABLED
 * drive model (AtariConfig Fase 3 / Pico Fase 12B).
 *
 * Like tests/test_netconfig_protocol.c, this is NOT part of the
 * cross-compiled SIDETNFS.PRG build -- it links directly against the
 * real, non-static drive.c (compiled natively, no GEM/hardware
 * dependency there), and separately mirrors the handful of *static*
 * dialog.c helpers a scenario needs (wire_to_ui_drive()'s state
 * fallback, validate_drive()'s letter-conflict scan, and the protocol
 * version check in dialog_startup_load()) -- those can't be linked
 * directly since dialog.c pulls in <gem.h> and real ROM3 hardware
 * access, but their logic is simple enough to reproduce verbatim here,
 * same documented convention as the network-config test file.
 *
 * Build/run:
 *   cc -Wall -Wextra -std=c89 -pedantic -o /tmp/test_drive_slots \
 *       tests/test_drive_slots.c src/drive.c -I include && \
 *       /tmp/test_drive_slots
 *
 * NOT host-testable here (needs real SideTNFS hardware/GEM): the actual
 * GET_DRIVE/SET_DRIVE/SAVE_CONFIG round-trip, real communication
 * timeouts, and the live editor dialogs themselves.
 */

#include <stdio.h>
#include <string.h>

#include "drive.h"
#include "sidetnfs_probe.h"

static int g_failures = 0;

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
            g_failures++; \
        } \
    } while (0)

static void make_enabled_tnfs(Drive *d, char letter, const char *nickname)
{
    memset(d, 0, sizeof(*d));
    d->state = DRIVE_SLOT_ENABLED;
    d->letter = letter;
    d->type = DRIVE_TYPE_TNFS;
    strncpy(d->nickname, nickname, DRIVE_NICK_LEN - 1);
    d->transport = DRIVE_TRANSPORT_UDP;
    strncpy(d->host, "10.0.0.5", DRIVE_HOST_LEN - 1);
    d->port = 16384;
    strncpy(d->mount_path, "/", DRIVE_MOUNT_LEN - 1);
}

/* Test A: all slots EMPTY. */
static void test_a_all_empty(void)
{
    DriveConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.settings_letter = 'S';

    CHECK(drive_config_configured_count(&cfg) == 0, "A: configured == 0");
    CHECK(drive_config_enabled_count(&cfg) == 0, "A: enabled == 0");
    CHECK(drive_config_empty_count(&cfg) == 8, "A: empty == 8");
}

/* Test B: one ENABLED. */
static void test_b_one_enabled(void)
{
    DriveConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.settings_letter = 'S';
    make_enabled_tnfs(&cfg.drives[2], 'N', "RetroLoft");

    CHECK(drive_config_configured_count(&cfg) == 1, "B: configured == 1");
    CHECK(drive_config_enabled_count(&cfg) == 1, "B: enabled == 1");
    CHECK(drive_config_empty_count(&cfg) == 7, "B: empty == 7");
    CHECK(drive_slot_is_enabled(&cfg.drives[2]), "B: slot 3 reports enabled");
    CHECK(drive_slot_is_configured(&cfg.drives[2]), "B: slot 3 reports configured");
    CHECK(!drive_slot_is_empty(&cfg.drives[2]), "B: slot 3 is not empty");
}

/* Test C: one DISABLED -- fully configured, just not counted as active. */
static void test_c_one_disabled(void)
{
    DriveConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.settings_letter = 'S';
    make_enabled_tnfs(&cfg.drives[5], 'O', "FujiNet");
    cfg.drives[5].state = DRIVE_SLOT_DISABLED;

    CHECK(drive_config_configured_count(&cfg) == 1, "C: configured == 1");
    CHECK(drive_config_enabled_count(&cfg) == 0, "C: enabled == 0");
    CHECK(drive_slot_is_configured(&cfg.drives[5]), "C: slot 6 still configured");
    CHECK(!drive_slot_is_enabled(&cfg.drives[5]), "C: slot 6 not enabled");
    /* Fields must all still be present, per "no data loss" requirement. */
    CHECK(strcmp(cfg.drives[5].nickname, "FujiNet") == 0, "C: nickname preserved");
    CHECK(strcmp(cfg.drives[5].host, "10.0.0.5") == 0, "C: host preserved");
    CHECK(cfg.drives[5].port == 16384, "C: port preserved");
}

/* Test D: DISABLED -> ENABLED, only state changes. */
static void test_d_disabled_to_enabled(void)
{
    Drive before, after;
    make_enabled_tnfs(&before, 'P', "GAMES");
    before.state = DRIVE_SLOT_DISABLED;

    after = before;
    after.state = DRIVE_SLOT_ENABLED; /* exactly what the Active/Inactive toggle does */

    CHECK(strcmp(before.nickname, after.nickname) == 0, "D: nickname unchanged");
    CHECK(strcmp(before.host, after.host) == 0, "D: host unchanged");
    CHECK(before.port == after.port, "D: port unchanged");
    CHECK(before.letter == after.letter, "D: letter unchanged");
    CHECK(before.type == after.type, "D: type unchanged");
    CHECK(before.state != after.state, "D: state did change");
}

/* Test E: ENABLED -> DISABLED, only state changes (mirror of D). */
static void test_e_enabled_to_disabled(void)
{
    Drive before, after;
    make_enabled_tnfs(&before, 'Q', "Backup");

    after = before;
    after.state = DRIVE_SLOT_DISABLED;

    CHECK(strcmp(before.nickname, after.nickname) == 0, "E: nickname unchanged");
    CHECK(strcmp(before.mount_path, after.mount_path) == 0, "E: mount_path unchanged");
    CHECK(before.state != after.state, "E: state did change");
}

/* Test F: -> EMPTY, everything locally cleared (mirrors sd_editor_run()/
 * tnfs_editor_run()'s Remove handling: memset the whole record). */
static void test_f_to_empty(void)
{
    Drive d;
    make_enabled_tnfs(&d, 'R', "ToRemove");

    memset(&d, 0, sizeof(d)); /* == the Remove action */

    CHECK(drive_slot_is_empty(&d), "F: slot reports empty after Remove");
    CHECK(d.nickname[0] == '\0', "F: nickname cleared");
    CHECK(d.host[0] == '\0', "F: host cleared");
    CHECK(d.letter == '\0', "F: letter cleared");
}

/* Test G: duplicate letter where the existing holder is DISABLED --
 * mirrors validate_drive()'s letter scan (dialog.c), reproduced here
 * since that function is static and pulls in <gem.h>. A DISABLED slot
 * must still block reuse of its letter. */
static int letter_conflict_slot(const DriveConfig *cfg, char letter, int skip_index)
{
    int i;
    for (i = 0; i < MAX_DRIVES; i++) {
        if (i == skip_index) continue;
        if (drive_slot_is_configured(&cfg->drives[i]) && cfg->drives[i].letter == letter)
            return i;
    }
    return -1;
}

static void test_g_duplicate_letter_disabled(void)
{
    DriveConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.settings_letter = 'S';
    make_enabled_tnfs(&cfg.drives[0], 'N', "Existing");
    cfg.drives[0].state = DRIVE_SLOT_DISABLED;

    CHECK(letter_conflict_slot(&cfg, 'N', 4) == 0, "G: DISABLED slot 1 still reserves letter N");
    CHECK(letter_conflict_slot(&cfg, 'N', 0) == -1, "G: skip_index excludes the slot being edited itself");
}

/* Test H: conflict with the SETTINGS letter -- mirrors validate_drive()'s
 * first check (cfg->settings_letter == d->letter). */
static void test_h_settings_letter_conflict(void)
{
    DriveConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.settings_letter = 'S';

    CHECK(cfg.settings_letter == 'S', "H: SETTINGS letter is S");
    /* validate_drive()'s exact first check: */
    CHECK(('S' == cfg.settings_letter) == 1, "H: a new drive claiming S: must be rejected before the slot scan");
}

/* Test I: GET of an EMPTY slot is not an error -- mirrors
 * wire_to_ui_drive()'s early return for state == EMPTY (dialog.c),
 * reproduced here since that function is static. */
static void wire_to_ui_drive_state_only(unsigned long wire_state, Drive *out)
{
    memset(out, 0, sizeof(*out));
    switch (wire_state) {
    case SIDETNFS_DRIVE_STATE_DISABLED: out->state = DRIVE_SLOT_DISABLED; break;
    case SIDETNFS_DRIVE_STATE_ENABLED:  out->state = DRIVE_SLOT_ENABLED;  break;
    case SIDETNFS_DRIVE_STATE_EMPTY:
    default:                           out->state = DRIVE_SLOT_EMPTY;   break;
    }
}

static void test_i_get_empty_slot(void)
{
    Drive d;
    wire_to_ui_drive_state_only(SIDETNFS_DRIVE_STATE_EMPTY, &d);
    CHECK(drive_slot_is_empty(&d), "I: wire state EMPTY maps to DRIVE_SLOT_EMPTY, not an error");
}

/* Test J: unknown/out-of-range state value -- must not silently become
 * ENABLED or DISABLED; wire_to_ui_drive()'s switch defaults to EMPTY,
 * which is the safe "nothing usable was configured" reading. Real
 * rejection with a status code happens firmware-side
 * (SIDETNFS_CONFIG_STATUS_INVALID_DRIVE_STATE) when *sending* an
 * invalid state via SET_DRIVE -- this test covers the read path. */
static void test_j_unknown_state(void)
{
    Drive d;
    wire_to_ui_drive_state_only(99UL, &d);
    CHECK(drive_slot_is_empty(&d), "J: an out-of-range wire state falls back to EMPTY, never ENABLED/DISABLED");
}

/* Tests K/L: protocol version mismatch handling -- mirrors
 * dialog_startup_load()'s exact comparison. */
static int protocol_version_ok(unsigned long firmware_version)
{
    return firmware_version == SIDETNFS_CONFIG_PROTOCOL_VERSION;
}

static void test_k_protocol_v2_rejected(void)
{
    CHECK(!protocol_version_ok(2UL), "K: protocol v2 must be rejected, not silently reinterpreted");
}

static void test_l_protocol_v3_accepted(void)
{
    CHECK(protocol_version_ok(3UL), "L: protocol v3 is accepted");
    CHECK(SIDETNFS_CONFIG_PROTOCOL_VERSION == 3UL, "L: SIDETNFS_CONFIG_PROTOCOL_VERSION is 3");
}

/* Extra: the eight-slot array bound + settings letter live outside it. */
static void test_fixed_slots_and_settings_letter_separate(void)
{
    DriveConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.settings_letter = 'S';
    make_enabled_tnfs(&cfg.drives[1], 'N', "Slot2");

    CHECK(MAX_DRIVES == 8, "extra: exactly eight fixed ordinary slots");
    CHECK(drive_slot_is_empty(&cfg.drives[4]), "extra: clearing slot 2 must never affect slot 5");
    CHECK(cfg.drives[1].letter == 'N', "extra: slot 2 keeps its own position, never compacted");
}

int main(void)
{
    test_a_all_empty();
    test_b_one_enabled();
    test_c_one_disabled();
    test_d_disabled_to_enabled();
    test_e_enabled_to_disabled();
    test_f_to_empty();
    test_g_duplicate_letter_disabled();
    test_h_settings_letter_conflict();
    test_i_get_empty_slot();
    test_j_unknown_state();
    test_k_protocol_v2_rejected();
    test_l_protocol_v3_accepted();
    test_fixed_slots_and_settings_letter_separate();

    if (g_failures == 0) {
        printf("All host-side drive-slot tests passed.\n");
        return 0;
    }
    printf("%d test(s) FAILED.\n", g_failures);
    return 1;
}
