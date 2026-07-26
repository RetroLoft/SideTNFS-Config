#ifndef DRIVE_H
#define DRIVE_H

/* Fase 12B/AtariConfig-3: eight FIXED ordinary drive slots (index ==
 * firmware slot index, 0..7), each in one of three states -- mirrors the
 * firmware's sidetnfs_drive_slot_state_t exactly (romemul/include/
 * sidetnfs_config.h). Slots are never removed from the array, never
 * compacted, and never auto-sorted: slot 5 stays slot 5 when slot 2 is
 * cleared. The SETTINGS disk (always active, read-only, letter only) is
 * NOT one of these eight slots -- it is DriveConfig's own separate
 * settings_letter field, matching the firmware's config_drive_letter. */

#define MAX_ORDINARY_DRIVES 8
#define MAX_DRIVES           MAX_ORDINARY_DRIVES /* kept as a name for the existing call sites */

#define DRIVE_NICK_LEN   24 /* 23 chars + NUL */
#define DRIVE_HOST_LEN   64 /* 63 chars + NUL */
#define DRIVE_MOUNT_LEN  32 /* 31 chars + NUL */
#define DRIVE_SDPATH_LEN 64

/* Matches sidetnfs_drive_slot_state_t (sidetnfs_config.h) value-for-value.
 * EMPTY: no stored configuration, every other field meaningless/zeroed.
 * DISABLED: a fully valid, stored configuration, deliberately not
 * published as a GEMDOS drive -- still reserves its drive letter.
 * ENABLED: a fully valid, stored configuration that is (eventually)
 * published. DISABLED and ENABLED are both "configured". */
typedef enum {
    DRIVE_SLOT_EMPTY    = 0,
    DRIVE_SLOT_DISABLED = 1,
    DRIVE_SLOT_ENABLED  = 2
} DriveSlotState;

/* Matches sidetnfs_drive_type_t (sidetnfs_config.h) -- no "CONFIG" value
 * here, since the SETTINGS disk is no longer part of this model at all. */
typedef enum {
    DRIVE_TYPE_SD   = 1,
    DRIVE_TYPE_TNFS = 2
} DriveType;

#define DRIVE_TRANSPORT_UDP 0
#define DRIVE_TRANSPORT_TCP 1 /* visible, always rejected this phase */

typedef struct {
    DriveSlotState state;
    char      letter;    /* meaningless when state == DRIVE_SLOT_EMPTY */
    DriveType type;       /* meaningless when state == DRIVE_SLOT_EMPTY */
    char      nickname[DRIVE_NICK_LEN];

    /* TNFS-specific */
    int       transport;
    char      host[DRIVE_HOST_LEN];
    int       port;
    char      mount_path[DRIVE_MOUNT_LEN];

    /* SD-specific */
    char      sd_path[DRIVE_SDPATH_LEN];
} Drive;

typedef struct {
    Drive drives[MAX_DRIVES]; /* fixed slots 0..7, never compacted/sorted */
    char  settings_letter;    /* SETTINGS disk drive letter, e.g. 'S' -- always active, not part of drives[] */
} DriveConfig;

/* Single source of truth for the three-state semantics -- never compare
 * ->state directly against DRIVE_SLOT_* elsewhere (mirrors the firmware's
 * own sidetnfs_drive_slot_is_empty()/_is_configured()/_is_enabled()). */
int drive_slot_is_empty(const Drive *d);
int drive_slot_is_configured(const Drive *d); /* DISABLED or ENABLED */
int drive_slot_is_enabled(const Drive *d);

/* Builds the built-in defaults: SETTINGS letter 'S', slot 0 the
 * RetroLoft TNFS drive (ENABLED), slots 1-7 EMPTY. */
void drive_config_init_defaults(DriveConfig *cfg);

/* Local, recomputed-on-demand counts -- see the report for why these are
 * never trusted from a firmware field instead. */
int drive_config_configured_count(const DriveConfig *cfg); /* DISABLED + ENABLED */
int drive_config_enabled_count(const DriveConfig *cfg);    /* ENABLED only */
int drive_config_empty_count(const DriveConfig *cfg);       /* EMPTY only */

/* 1 if `letter` (already uppercased) is used by the SETTINGS disk or by
 * any CONFIGURED (DISABLED or ENABLED) slot other than drives[skip_index]
 * (pass -1 to check against every slot). A DISABLED slot still reserves
 * its letter -- see drive_slot_is_configured(). dialog.c's own
 * validate_drive()/cl_editor_run() do their own field-by-field letter
 * scan instead of calling this directly, since they need to report
 * *which* slot (or the SETTINGS disk) a conflict is against -- this
 * helper only answers yes/no, and is used internally by
 * drive_config_suggest_letter() below. */
int drive_config_letter_in_use(const DriveConfig *cfg, char letter, int skip_index);

/* First free letter C-Z not in use, or '\0' if none (8 drive slots plus
 * the SETTINGS letter cannot exhaust C-Z, so '\0' should not occur in
 * practice). */
char drive_config_suggest_letter(const DriveConfig *cfg);

#endif
