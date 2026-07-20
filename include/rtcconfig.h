#ifndef RTCCONFIG_H
#define RTCCONFIG_H

/* Fase 12 (Clock/NTP, v1 -- KISS): local-only RTC/NTP settings model,
 * same shape as netconfig.h. UI state only; the wire protocol
 * (GET/SET/SAVE_RTC_CONFIG, 0x0416-0x0418, Fase 12A) lives in
 * sidetnfs_probe.h/.c -- dialog.c translates between the two explicitly
 * (wire_to_ui_rtcconfig()/ui_to_wire_rtcconfig()), same as NetConfig.
 * No automatic timezone/DST/IANA/POSIX-TZ support: a plain whole-hour
 * UTC offset is all v1 supports.
 *
 * Firmware mapping (romemul/include/sidetnfs_rtcconfig.h):
 *   ntp_enabled -> GEMDRIVE_RTC
 *   ntp_server  -> RTC_NTP_SERVER_HOST
 *   utc_offset  -> RTC_UTC_OFFSET
 * The NTP port stays a fixed, internal 123 -- never shown in the UI. */

#define RTCCONFIG_NTP_SERVER_LEN 64 /* 63 chars + NUL */
#define RTCCONFIG_UTC_OFFSET_LEN 4  /* "-12"/"+14" (3 chars) + NUL */

typedef struct {
    int  ntp_enabled;
    char ntp_server[RTCCONFIG_NTP_SERVER_LEN];
    char utc_offset[RTCCONFIG_UTC_OFFSET_LEN]; /* normalized: "0", "+1", "-5", "+14" */
} RtcConfig;

/* In-memory defaults only -- no file/firmware I/O. */
void rtcconfig_init_defaults(RtcConfig *cfg);

#endif
