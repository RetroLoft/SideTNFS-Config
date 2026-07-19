#ifndef SIDETNFS_PROBE_H
#define SIDETNFS_PROBE_H

/* Fase AC-4: SideTNFS config-protocol version 2. Six commands, all
 * cross-checked against sd2tnfs/docs/sidetnfs-config-protocol.md and the
 * firmware headers it documents -- no offsets/lengths/statuses guessed.
 * This module is a pure protocol layer: it knows nothing about the UI's
 * DriveConfig/Drive model (drive.h). dialog.c translates explicitly
 * between the two. */

#define SIDETNFS_PROBE_OK      0
#define SIDETNFS_PROBE_TIMEOUT 1

#define SIDETNFS_NICKNAME_LEN   24
#define SIDETNFS_HOST_LEN       64
#define SIDETNFS_MOUNTPATH_LEN  32
#define SIDETNFS_SDPATH_LEN     64

#define SIDETNFS_DRIVE_TYPE_NONE 0
#define SIDETNFS_DRIVE_TYPE_SD   1
#define SIDETNFS_DRIVE_TYPE_TNFS 2

#define SIDETNFS_TRANSPORT_UDP 0
#define SIDETNFS_TRANSPORT_TCP 1

/* protocol/status codes -- sidetnfs-config-protocol.md "Statuscodes" */
#define SIDETNFS_STATUS_OK                     0
#define SIDETNFS_STATUS_INVALID_INDEX          1
#define SIDETNFS_STATUS_EMPTY_SLOT             2
#define SIDETNFS_STATUS_INVALID_DRIVE_LETTER   3
#define SIDETNFS_STATUS_DUPLICATE_DRIVE_LETTER 4
#define SIDETNFS_STATUS_INVALID_TYPE           5
#define SIDETNFS_STATUS_INVALID_TRANSPORT      6
#define SIDETNFS_STATUS_INVALID_PORT           7
#define SIDETNFS_STATUS_INVALID_HOST           8
#define SIDETNFS_STATUS_INVALID_MOUNT_PATH     9
#define SIDETNFS_STATUS_INVALID_SD_PATH        10
#define SIDETNFS_STATUS_TOO_MANY_DRIVES        11
#define SIDETNFS_STATUS_FLASH_WRITE_FAILED     12
#define SIDETNFS_STATUS_CRC_MISMATCH           13
#define SIDETNFS_STATUS_UNSUPPORTED_VERSION    14

typedef struct {
    unsigned long protocol_version;
    unsigned long max_drives;         /* ordinary drives only, excludes the config drive */
    unsigned long drive_count;
    unsigned long config_drive_letter; /* ASCII code */
    unsigned long status;
} SideTnfsConfigInfo;

/* Wire record shared by GET_DRIVE (full) and SET_DRIVE (request, minus
 * status). Field lengths match the firmware's sidetnfs_drive_config_t
 * exactly (see sidetnfs_config.h), independent of the UI's drive.h
 * lengths even though the numbers happen to match today. */
typedef struct {
    unsigned long status;    /* only meaningful after GET_DRIVE/SET_DRIVE etc return OK */
    unsigned long used;
    unsigned long letter;    /* ASCII code */
    unsigned long type;      /* SIDETNFS_DRIVE_TYPE_* */
    unsigned long transport; /* SIDETNFS_TRANSPORT_* */
    unsigned long port;
    char nickname[SIDETNFS_NICKNAME_LEN];
    char host[SIDETNFS_HOST_LEN];
    char mount_path[SIDETNFS_MOUNTPATH_LEN];
    char sd_path[SIDETNFS_SDPATH_LEN];
} SideTnfsDriveInfo;

/* Every function below returns SIDETNFS_PROBE_OK / SIDETNFS_PROBE_TIMEOUT
 * for the communication result. The firmware's own protocol status (OK /
 * INVALID_INDEX / ... ) is a separate result, returned via *info->status
 * (GET_CONFIG_INFO/GET_DRIVE) or *out_status (the write commands) -- a
 * SIDETNFS_PROBE_OK communication result says nothing about whether the
 * firmware accepted the request. */

int sidetnfs_probe_get_config_info(SideTnfsConfigInfo *info);
int sidetnfs_probe_get_drive(unsigned long index, SideTnfsDriveInfo *info);
int sidetnfs_probe_set_drive(unsigned long index, const SideTnfsDriveInfo *in, unsigned long *out_status);
int sidetnfs_probe_delete_drive(unsigned long index, unsigned long *out_status);
int sidetnfs_probe_set_config_drive(unsigned long letter, unsigned long *out_status);
int sidetnfs_probe_save_config(unsigned long *out_status);

#endif
