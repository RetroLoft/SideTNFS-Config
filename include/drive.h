#ifndef DRIVE_H
#define DRIVE_H

/* Fase AC-3: internal drive-overview model. Field lengths for the
 * TNFS-specific fields match the already-agreed firmware wire protocol
 * (SIDETNFS_NICKNAME_LEN/HOST_LEN/MOUNTPATH_LEN in
 * sd2tnfs/romemul/include/sidetnfs_config.h), so this struct can later be
 * exchanged with the Pico without re-sizing. sd_path has no firmware
 * counterpart yet -- sized generously, not protocol-derived. */

#define MAX_DRIVES       8

#define DRIVE_NICK_LEN   24 /* 23 chars + NUL */
#define DRIVE_HOST_LEN   64 /* 63 chars + NUL */
#define DRIVE_MOUNT_LEN  32 /* 31 chars + NUL */
#define DRIVE_SDPATH_LEN 64

typedef enum {
    DRIVE_TYPE_CONFIG = 0,
    DRIVE_TYPE_SD     = 1,
    DRIVE_TYPE_TNFS   = 2
} DriveType;

#define DRIVE_TRANSPORT_UDP 0
#define DRIVE_TRANSPORT_TCP 1 /* visible, always rejected this phase */

typedef struct {
    int       used;
    char      letter;
    DriveType type;
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
    Drive drives[MAX_DRIVES];
    int   drive_count;
} DriveConfig;

/* Builds the two mandatory default rows: config drive S: and the
 * RetroLoft TNFS drive N:, already sorted by letter (N before S). */
void drive_config_init_defaults(DriveConfig *cfg);

/* Index of the (always-present) config drive, or -1 if the invariant was
 * somehow violated (defensive only -- there is always exactly one). Its
 * position in the array is NOT fixed: the list is kept sorted by drive
 * letter, so the config drive can be anywhere. */
int drive_config_config_index(const DriveConfig *cfg);

/* Stable-sorts drives[0..drive_count-1] by letter, ascending. Call after
 * any change that can affect a letter (add, edit, delete) so the
 * overview always lists drives in driveletter order. */
void drive_config_sort_by_letter(DriveConfig *cfg);

/* 1 if `letter` (already uppercased) is used by any drive other than
 * drives[skip_index] (pass -1 to check against all drives). */
int drive_config_letter_in_use(const DriveConfig *cfg, char letter, int skip_index);

/* 1 if `letter` is a valid, assignable drive letter: C-Z, and not already
 * used by another drive (skip_index excluded, pass -1 when adding new). */
int drive_config_letter_valid(const DriveConfig *cfg, char letter, int skip_index);

/* First free letter C-Z not in use, or '\0' if none (all 8 drive slots
 * cannot exhaust C-Z, so '\0' should not occur in practice). */
char drive_config_suggest_letter(const DriveConfig *cfg);

#endif
