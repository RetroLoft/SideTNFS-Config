#ifndef SIDETNFS_PROBE_H
#define SIDETNFS_PROBE_H

/* AtariConfig Fase 3: SideTNFS config-protocol version 3. Cross-checked
 * against sd2tnfs/romemul/include/{gemdrvemul.h,sidetnfs_config.h,
 * commands.h} and sidetnfs_config.c -- no offsets/lengths/statuses
 * guessed. This module is a pure protocol layer: it knows nothing about
 * the UI's DriveConfig/Drive model (drive.h). dialog.c translates
 * explicitly between the two.
 *
 * Fase 12B/AtariConfig-3: the drive record's wire layout (offsets, field
 * widths) is BYTE-IDENTICAL to protocol v2 -- only the first field's
 * name and value range changed: DRIVE_USED (0/1 bool) -> DRIVE_STATE
 * (0 EMPTY/1 DISABLED/2 ENABLED), same offset, same uint16_t plain-word
 * wire type. GET_DRIVE's own STATUS field also changed meaning: from
 * protocol v3 on it is OK for every valid index 0..SIDETNFS_MAX_DRIVES-1
 * regardless of state (EMPTY included) -- only an out-of-range index is
 * ever INVALID_INDEX now. EMPTY_SLOT is no longer returned by GET_DRIVE;
 * it can still be returned by DELETE_DRIVE (already-empty slot) and by
 * the firmware's internal set-state-only helper. */

#define SIDETNFS_PROBE_OK      0
#define SIDETNFS_PROBE_TIMEOUT 1

#define SIDETNFS_NICKNAME_LEN   24
#define SIDETNFS_HOST_LEN       64
#define SIDETNFS_MOUNTPATH_LEN  32
#define SIDETNFS_SDPATH_LEN     64

#define SIDETNFS_DRIVE_TYPE_NONE 0 /* EMPTY slot's zeroed type field reads back as this */
#define SIDETNFS_DRIVE_TYPE_SD   1
#define SIDETNFS_DRIVE_TYPE_TNFS 2

/* Matches sidetnfs_drive_slot_state_t (sidetnfs_config.h) value-for-value. */
#define SIDETNFS_DRIVE_STATE_EMPTY    0
#define SIDETNFS_DRIVE_STATE_DISABLED 1
#define SIDETNFS_DRIVE_STATE_ENABLED  2

#define SIDETNFS_TRANSPORT_UDP 0
#define SIDETNFS_TRANSPORT_TCP 1

/* protocol/status codes -- sidetnfs_config_status_t (sidetnfs_config.h) */
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
#define SIDETNFS_STATUS_INVALID_DRIVE_STATE    15 /* Fase 12B: drives[] record's state byte outside 0/1/2 */

#define SIDETNFS_CONFIG_PROTOCOL_VERSION 3UL

typedef struct {
    unsigned long protocol_version;
    unsigned long max_drives;         /* ordinary drives only, excludes the SETTINGS disk */
    unsigned long drive_count;        /* configured (DISABLED+ENABLED) ordinary-drive count, excludes EMPTY */
    unsigned long config_drive_letter; /* SETTINGS disk letter, ASCII code */
    unsigned long status;
} SideTnfsConfigInfo;

/* Wire record shared by GET_DRIVE (full) and SET_DRIVE (request, minus
 * status). Field lengths match the firmware's sidetnfs_drive_config_t
 * exactly (see sidetnfs_config.h), independent of the UI's drive.h
 * lengths even though the numbers happen to match today. */
typedef struct {
    unsigned long status; /* only meaningful after GET_DRIVE/SET_DRIVE etc return OK */
    unsigned long state;  /* SIDETNFS_DRIVE_STATE_* -- EMPTY/DISABLED/ENABLED */
    unsigned long letter;    /* ASCII code, meaningless when state == EMPTY */
    unsigned long type;      /* SIDETNFS_DRIVE_TYPE_*, meaningless when state == EMPTY */
    unsigned long transport; /* SIDETNFS_TRANSPORT_* */
    unsigned long port;
    char nickname[SIDETNFS_NICKNAME_LEN];
    char host[SIDETNFS_HOST_LEN];
    char mount_path[SIDETNFS_MOUNTPATH_LEN];
    char sd_path[SIDETNFS_SDPATH_LEN];
} SideTnfsDriveInfo;

/* Fase AC-4 (wifi/network): field lengths match the firmware's
 * sidetnfs_network_config_t exactly (romemul/include/sidetnfs_netconfig.h
 * via network.h), independent of the UI's netconfig.h lengths. */
#define SIDETNFS_NET_SSID_LEN     36
#define SIDETNFS_NET_PASSWORD_LEN 68
#define SIDETNFS_NET_COUNTRY_LEN  4
#define SIDETNFS_NET_IPV4_LEN     16

/* romemul/include/sidetnfs_netconfig.h sidetnfs_netconfig_status_t */
#define SIDETNFS_NETCONFIG_STATUS_OK                  0
#define SIDETNFS_NETCONFIG_STATUS_INVALID_SSID        1
#define SIDETNFS_NETCONFIG_STATUS_INVALID_PASSWORD    2
#define SIDETNFS_NETCONFIG_STATUS_INVALID_AUTH_MODE   3
#define SIDETNFS_NETCONFIG_STATUS_INVALID_COUNTRY     4
#define SIDETNFS_NETCONFIG_STATUS_INVALID_DHCP        5
#define SIDETNFS_NETCONFIG_STATUS_INVALID_IP          6
#define SIDETNFS_NETCONFIG_STATUS_INVALID_NETMASK     7
#define SIDETNFS_NETCONFIG_STATUS_INVALID_GATEWAY     8
#define SIDETNFS_NETCONFIG_STATUS_INVALID_DNS         9
#define SIDETNFS_NETCONFIG_STATUS_NOT_STAGED          10
#define SIDETNFS_NETCONFIG_STATUS_FLASH_WRITE_FAILED  11
#define SIDETNFS_NETCONFIG_STATUS_FLASH_VERIFY_FAILED 12

/* Wire record shared by GET_NETWORK_CONFIG (full) and SET_NETWORK_CONFIG
 * (request, minus status). */
typedef struct {
    unsigned long status;    /* only meaningful after GET/SET return OK */
    unsigned long auth_mode; /* 0-8, raw firmware value */
    unsigned long use_dhcp;
    char ssid[SIDETNFS_NET_SSID_LEN];
    char password[SIDETNFS_NET_PASSWORD_LEN];
    char country[SIDETNFS_NET_COUNTRY_LEN];
    char ip_address[SIDETNFS_NET_IPV4_LEN];
    char netmask[SIDETNFS_NET_IPV4_LEN];
    char gateway[SIDETNFS_NET_IPV4_LEN];
    char primary_dns[SIDETNFS_NET_IPV4_LEN];
} SideTnfsNetworkConfig;

/* Fase 12A (Clock/NTP): field lengths match the firmware's
 * sidetnfs_rtc_config_t exactly (romemul/include/sidetnfs_rtcconfig.h),
 * cross-checked against romemul/include/gemdrvemul.h's
 * GEMDRVEMUL_SIDETNFS_RTC_* offsets. */
#define SIDETNFS_RTC_NTP_SERVER_LEN 64
#define SIDETNFS_RTC_UTC_OFFSET_LEN 4

/* romemul/include/sidetnfs_rtcconfig.h sidetnfs_rtcconfig_status_t */
#define SIDETNFS_RTCCONFIG_STATUS_OK                  0
#define SIDETNFS_RTCCONFIG_STATUS_INVALID_ENABLED     1
#define SIDETNFS_RTCCONFIG_STATUS_INVALID_NTP_SERVER  2
#define SIDETNFS_RTCCONFIG_STATUS_INVALID_UTC_OFFSET  3
#define SIDETNFS_RTCCONFIG_STATUS_NOT_STAGED          4
#define SIDETNFS_RTCCONFIG_STATUS_FLASH_WRITE_FAILED  5
#define SIDETNFS_RTCCONFIG_STATUS_FLASH_VERIFY_FAILED 6

/* Wire record shared by GET_RTC_CONFIG (full) and SET_RTC_CONFIG
 * (request, minus status). */
typedef struct {
    unsigned long status;  /* only meaningful after GET/SET return OK */
    unsigned long enabled; /* 0 or 1 */
    char ntp_server[SIDETNFS_RTC_NTP_SERVER_LEN];
    char utc_offset[SIDETNFS_RTC_UTC_OFFSET_LEN];
} SideTnfsRtcConfig;

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

int sidetnfs_probe_get_network_config(SideTnfsNetworkConfig *info);
int sidetnfs_probe_set_network_config(const SideTnfsNetworkConfig *in, unsigned long *out_status);
int sidetnfs_probe_save_network_config(unsigned long *out_status);

int sidetnfs_probe_get_rtc_config(SideTnfsRtcConfig *info);
int sidetnfs_probe_set_rtc_config(const SideTnfsRtcConfig *in, unsigned long *out_status);
int sidetnfs_probe_save_rtc_config(unsigned long *out_status);

/* Live RTC/NTP synchronisation state, as opposed to the stored settings
 * GET_RTC_CONFIG returns. Read straight from the two status longs the
 * Pico publishes in the ROM3 exchange area and the GEMDRIVE ROM already
 * acts on; no command is sent, so this never blocks and never times out.
 *
 * SYNCED means the Pico explicitly published a synchronised date/time
 * this boot -- it is never inferred from a plausible Atari date, from a
 * live network connection, or from "NTP enabled" alone. */
#define SIDETNFS_RTC_SYNC_DISABLED   0 /* RTC/NTP switched off in the firmware */
#define SIDETNFS_RTC_SYNC_SYNCED     1 /* Pico published a synchronised clock */
#define SIDETNFS_RTC_SYNC_NOT_SYNCED 2 /* enabled, but never synchronised */
int sidetnfs_probe_get_rtc_sync_state(void);

/* Live WiFi link state, the network counterpart of the RTC sync state
 * above and read the same way -- no command is sent. CONNECTED means the
 * Pico brought WiFi up during its own boot; nothing in the protocol
 * reports an association lost after that, so this is a boot-time
 * snapshot rather than a live link poll. */
#define SIDETNFS_NET_LINK_NOT_CONNECTED 0
#define SIDETNFS_NET_LINK_CONNECTED     1
int sidetnfs_probe_get_network_link_state(void);

/* Fase 9B: controlled Pico reboot (GEMDRVEMUL_REBOOT_PICO, 0x041B). No
 * request payload, no response status field -- the firmware ACKs with
 * the same generic random-token handshake every other command uses
 * (write_random_token()), written BEFORE its own short fixed delay and
 * watchdog_reboot(), then never SAVEs anything (drive/network/RTC
 * config and flash defaults are untouched -- only the already-persisted
 * configuration is (re)loaded on the next boot). Returns
 * SIDETNFS_PROBE_OK once the ACK is seen, or SIDETNFS_PROBE_TIMEOUT if
 * it never arrives -- e.g. firmware older than this command, which
 * never recognizes 0x041B and so never ACKs it. Sends the request
 * exactly once; never retried, so it can never arm a second reboot. */
int sidetnfs_probe_reboot_pico(void);

#endif
