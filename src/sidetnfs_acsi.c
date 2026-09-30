/*
 * ACSI transport for the SideTNFS configuration protocol.
 *
 * Drop-in replacement for sidetnfs_probe.c (same API, sidetnfs_probe.h),
 * used by the ACSI build ('make acsi' -> ACSITNFS.PRG). Instead of the
 * cartridge's ROM3 exchange area it talks to an ACSI2TNFS adapter
 * (github.com/RetroLoft/ACSI2TNFS) on the ACSI/DMA port:
 *
 *   vendor command [0x11|id<<5, 'A', 'T', sub, 0, 0]
 *     sub 5: DMA write, 512 bytes  [cmd u16][seq u16][request fields]
 *     sub 6: DMA read,  512 bytes  [cmd u16][seq u16][response fields]
 *
 * Command numbers, field order, field lengths and status codes are those
 * of the SideTNFS config protocol v3; numbers are big-endian 32-bit (the
 * 68000's native order), strings fixed length. The adapter answers
 * asynchronously: sub 6 is polled until cmd and seq match the request.
 *
 * The adapter is found with INQUIRY on ACSI ids 0..7 (product
 * "ACSI2TNFS"). All DMA work runs in supervisor mode via Supexec and
 * honours the floppy lock (flock) like any hard disk driver.
 */
#include <string.h>
#include <osbind.h>
#include "sidetnfs_probe.h"

#define CMD_GET_CONFIG_INFO     0x040DU
#define CMD_GET_DRIVE           0x040EU
#define CMD_SET_DRIVE           0x040FU
#define CMD_DELETE_DRIVE        0x0410U
#define CMD_SET_CONFIG_DRIVE    0x0411U
#define CMD_SAVE_CONFIG         0x0412U
#define CMD_GET_NETWORK_CONFIG  0x0413U
#define CMD_SET_NETWORK_CONFIG  0x0414U
#define CMD_SAVE_NETWORK_CONFIG 0x0415U
#define CMD_GET_RTC_CONFIG      0x0416U
#define CMD_SET_RTC_CONFIG      0x0417U
#define CMD_SAVE_RTC_CONFIG     0x0418U
#define CMD_REBOOT_PICO         0x041BU
#define CMD_CHECK_UPDATE        0x041CU
#define CMD_GET_STATUS          0x0480U     /* ACSI2TNFS: live clock/link state */

#define PROBE_TIMEOUT_SEC        5
#define SAVE_CONFIG_TIMEOUT_SEC  10
#define UPDATE_CHECK_TIMEOUT_SEC 30

/* ---------------- hardware ---------------- */

#define DMA_DATA    (*(volatile unsigned short *)0xFFFF8604L)
#define DMA_DATAL   (*(volatile unsigned long  *)0xFFFF8604L)  /* data + mode in one access */
#define DMA_MODE    (*(volatile unsigned short *)0xFFFF8606L)
#define DMA_HI      (*(volatile unsigned char  *)0xFFFF8609L)
#define DMA_MID     (*(volatile unsigned char  *)0xFFFF860BL)
#define DMA_LO      (*(volatile unsigned char  *)0xFFFF860DL)
#define GPIP        (*(volatile unsigned char  *)0xFFFFFA01L)
#define HZ200       (*(volatile unsigned long  *)0x4BAL)
#define FLOCK       (*(volatile short          *)0x43EL)

static unsigned char dmabuf[512] __attribute__((aligned(2)));  /* ST-RAM (program BSS) */

/* parameters of the current ACSI command, set before Supexec */
static unsigned char a_cmd[6];
static unsigned short a_write;

static int wait_irq(unsigned long ticks)
{
    unsigned long end = HZ200 + ticks;
    while (GPIP & 0x20) {
        if ((long)(HZ200 - end) > 0)
            return -1;
    }
    return 0;
}

/* one 6-byte ACSI command with a 1-sector DMA into/out of dmabuf.
   Returns the status byte, or -1 on timeout. Supervisor mode. */
static long acsi_exec(void)
{
    unsigned long addr = (unsigned long)dmabuf;
    unsigned short base, dir;
    long st = -1;
    int i;

    FLOCK = -1;
    DMA_LO  = (unsigned char)addr;
    DMA_MID = (unsigned char)(addr >> 8);
    DMA_HI  = (unsigned char)(addr >> 16);

    base = a_write ? 0x190 : 0x090;
    DMA_MODE = base ^ 0x100;            /* toggle R/W: reset DMA, flush FIFO */
    DMA_MODE = base;                    /* sector count register */
    DMA_DATA = 1;
    dir = base & 0x100;

    DMA_MODE = dir | 0x88;              /* A1 = 0: first command byte */
    for (i = 0; i < 5; i++) {
        DMA_DATAL = ((unsigned long)a_cmd[i] << 16) | (dir | 0x8a);
        if (wait_irq(20))
            goto out;
    }
    DMA_DATAL = ((unsigned long)a_cmd[5] << 16) | dir;   /* last byte, start DMA */
    if (wait_irq(400))
        goto out;
    DMA_MODE = dir | 0x8a;
    st = DMA_DATA & 0xff;
out:
    DMA_MODE = 0x80;
    FLOCK = 0;
    return st;
}

static long acsi(void)
{
    return Supexec(acsi_exec);
}

/* ---------------- adapter discovery ---------------- */

static int acsi_id = -1;

static int find_adapter(void)
{
    int id;
    if (acsi_id >= 0)
        return 1;
    for (id = 0; id < 8; id++) {
        memset(dmabuf, 0, sizeof dmabuf);
        a_cmd[0] = (unsigned char)((id << 5) | 0x12);   /* INQUIRY */
        a_cmd[1] = a_cmd[2] = a_cmd[3] = a_cmd[5] = 0;
        a_cmd[4] = 48;
        a_write = 0;
        if (acsi() == 0 && memcmp(dmabuf + 16, "ACSI2TNFS", 9) == 0) {
            acsi_id = id;
            return 1;
        }
    }
    return 0;
}

static long vendor(unsigned char sub, int write)
{
    a_cmd[0] = (unsigned char)((acsi_id << 5) | 0x11);
    a_cmd[1] = 'A';
    a_cmd[2] = 'T';
    a_cmd[3] = sub;
    a_cmd[4] = 0;
    a_cmd[5] = 0;
    a_write = (unsigned short)write;
    return acsi();
}

/* ---------------- request / response ---------------- */

static unsigned short seq;
static unsigned char req[512], rsp[512];
static unsigned char *wp;                 /* request writer  */
static const unsigned char *rp;           /* response reader */

static void req_start(unsigned short cmd)
{
    memset(req, 0, sizeof req);
    if (seq == 0)                         /* random start: never match a reply of a previous run */
        seq = (unsigned short)(Random() | 1);
    seq++;
    req[0] = (unsigned char)(cmd >> 8);
    req[1] = (unsigned char)cmd;
    req[2] = (unsigned char)(seq >> 8);
    req[3] = (unsigned char)seq;
    wp = req + 4;
}

static void put32(unsigned long v)
{
    wp[0] = (unsigned char)(v >> 24);
    wp[1] = (unsigned char)(v >> 16);
    wp[2] = (unsigned char)(v >> 8);
    wp[3] = (unsigned char)v;
    wp += 4;
}

static void putstr(const char *s, int len)
{
    strncpy((char *)wp, s, (size_t)len - 1);
    wp += len;
}

static unsigned long get32(void)
{
    unsigned long v = ((unsigned long)rp[0] << 24) | ((unsigned long)rp[1] << 16)
                    | ((unsigned long)rp[2] << 8) | rp[3];
    rp += 4;
    return v;
}

static void getstr(char *dst, int len)
{
    memcpy(dst, rp, (size_t)len);
    dst[len - 1] = 0;
    rp += len;
}

/* send req, poll for the matching response; SIDETNFS_PROBE_OK/_TIMEOUT */
static int transact(int timeout_sec)
{
    long frames = (long)timeout_sec * 72L;  /* VBLs: at least timeout_sec at 50/60/72 Hz */

    if (!find_adapter())
        return SIDETNFS_PROBE_TIMEOUT;
    memcpy(dmabuf, req, sizeof req);
    if (vendor(5, 1) != 0)
        return SIDETNFS_PROBE_TIMEOUT;

    while (frames-- > 0) {
        if (vendor(6, 0) == 0 && memcmp(dmabuf, req, 4) == 0) {
            memcpy(rsp, dmabuf, sizeof rsp);
            rp = rsp + 4;
            return SIDETNFS_PROBE_OK;
        }
        Vsync();
    }
    return SIDETNFS_PROBE_TIMEOUT;
}

/* ---------------- API (sidetnfs_probe.h) ---------------- */

int sidetnfs_probe_get_config_info(SideTnfsConfigInfo *info)
{
    req_start(CMD_GET_CONFIG_INFO);
    if (transact(PROBE_TIMEOUT_SEC) != SIDETNFS_PROBE_OK)
        return SIDETNFS_PROBE_TIMEOUT;
    info->protocol_version    = get32();
    info->max_drives          = get32();
    info->drive_count         = get32();
    info->config_drive_letter = get32();
    info->status              = get32();
    return SIDETNFS_PROBE_OK;
}

int sidetnfs_probe_get_drive(unsigned long index, SideTnfsDriveInfo *info)
{
    req_start(CMD_GET_DRIVE);
    put32(index);
    if (transact(PROBE_TIMEOUT_SEC) != SIDETNFS_PROBE_OK)
        return SIDETNFS_PROBE_TIMEOUT;
    info->status    = get32();
    info->state     = get32();
    info->letter    = get32();
    info->type      = get32();
    info->transport = get32();
    info->port      = get32();
    getstr(info->nickname,   SIDETNFS_NICKNAME_LEN);
    getstr(info->host,       SIDETNFS_HOST_LEN);
    getstr(info->mount_path, SIDETNFS_MOUNTPATH_LEN);
    getstr(info->sd_path,    SIDETNFS_SDPATH_LEN);
    return SIDETNFS_PROBE_OK;
}

int sidetnfs_probe_set_drive(unsigned long index, const SideTnfsDriveInfo *in, unsigned long *out_status)
{
    req_start(CMD_SET_DRIVE);
    put32(index);
    put32(in->state);
    put32(in->letter);
    put32(in->type);
    put32(in->transport);
    put32(in->port);
    putstr(in->nickname,   SIDETNFS_NICKNAME_LEN);
    putstr(in->host,       SIDETNFS_HOST_LEN);
    putstr(in->mount_path, SIDETNFS_MOUNTPATH_LEN);
    putstr(in->sd_path,    SIDETNFS_SDPATH_LEN);
    if (transact(PROBE_TIMEOUT_SEC) != SIDETNFS_PROBE_OK)
        return SIDETNFS_PROBE_TIMEOUT;
    *out_status = get32();
    return SIDETNFS_PROBE_OK;
}

static int simple(unsigned short cmd, int has_arg, unsigned long arg,
                  unsigned long *out_status, int timeout)
{
    req_start(cmd);
    if (has_arg)
        put32(arg);
    if (transact(timeout) != SIDETNFS_PROBE_OK)
        return SIDETNFS_PROBE_TIMEOUT;
    if (out_status)
        *out_status = get32();
    return SIDETNFS_PROBE_OK;
}

int sidetnfs_probe_delete_drive(unsigned long index, unsigned long *out_status)
{
    return simple(CMD_DELETE_DRIVE, 1, index, out_status, PROBE_TIMEOUT_SEC);
}

int sidetnfs_probe_set_config_drive(unsigned long letter, unsigned long *out_status)
{
    return simple(CMD_SET_CONFIG_DRIVE, 1, letter, out_status, PROBE_TIMEOUT_SEC);
}

int sidetnfs_probe_save_config(unsigned long *out_status)
{
    return simple(CMD_SAVE_CONFIG, 0, 0, out_status, SAVE_CONFIG_TIMEOUT_SEC);
}

int sidetnfs_probe_get_network_config(SideTnfsNetworkConfig *info)
{
    req_start(CMD_GET_NETWORK_CONFIG);
    if (transact(PROBE_TIMEOUT_SEC) != SIDETNFS_PROBE_OK)
        return SIDETNFS_PROBE_TIMEOUT;
    info->status    = get32();
    info->auth_mode = get32();
    info->use_dhcp  = get32();
    getstr(info->ssid,        SIDETNFS_NET_SSID_LEN);
    getstr(info->password,    SIDETNFS_NET_PASSWORD_LEN);
    getstr(info->country,     SIDETNFS_NET_COUNTRY_LEN);
    getstr(info->ip_address,  SIDETNFS_NET_IPV4_LEN);
    getstr(info->netmask,     SIDETNFS_NET_IPV4_LEN);
    getstr(info->gateway,     SIDETNFS_NET_IPV4_LEN);
    getstr(info->primary_dns, SIDETNFS_NET_IPV4_LEN);
    return SIDETNFS_PROBE_OK;
}

int sidetnfs_probe_set_network_config(const SideTnfsNetworkConfig *in, unsigned long *out_status)
{
    req_start(CMD_SET_NETWORK_CONFIG);
    put32(in->auth_mode);
    put32(in->use_dhcp);
    putstr(in->ssid,        SIDETNFS_NET_SSID_LEN);
    putstr(in->password,    SIDETNFS_NET_PASSWORD_LEN);
    putstr(in->country,     SIDETNFS_NET_COUNTRY_LEN);
    putstr(in->ip_address,  SIDETNFS_NET_IPV4_LEN);
    putstr(in->netmask,     SIDETNFS_NET_IPV4_LEN);
    putstr(in->gateway,     SIDETNFS_NET_IPV4_LEN);
    putstr(in->primary_dns, SIDETNFS_NET_IPV4_LEN);
    if (transact(PROBE_TIMEOUT_SEC) != SIDETNFS_PROBE_OK)
        return SIDETNFS_PROBE_TIMEOUT;
    *out_status = get32();
    return SIDETNFS_PROBE_OK;
}

int sidetnfs_probe_save_network_config(unsigned long *out_status)
{
    return simple(CMD_SAVE_NETWORK_CONFIG, 0, 0, out_status, SAVE_CONFIG_TIMEOUT_SEC);
}

int sidetnfs_probe_get_rtc_config(SideTnfsRtcConfig *info)
{
    req_start(CMD_GET_RTC_CONFIG);
    if (transact(PROBE_TIMEOUT_SEC) != SIDETNFS_PROBE_OK)
        return SIDETNFS_PROBE_TIMEOUT;
    info->status  = get32();
    info->enabled = get32();
    getstr(info->ntp_server, SIDETNFS_RTC_NTP_SERVER_LEN);
    getstr(info->utc_offset, SIDETNFS_RTC_UTC_OFFSET_LEN);
    return SIDETNFS_PROBE_OK;
}

int sidetnfs_probe_set_rtc_config(const SideTnfsRtcConfig *in, unsigned long *out_status)
{
    req_start(CMD_SET_RTC_CONFIG);
    put32(in->enabled);
    putstr(in->ntp_server, SIDETNFS_RTC_NTP_SERVER_LEN);
    putstr(in->utc_offset, SIDETNFS_RTC_UTC_OFFSET_LEN);
    if (transact(PROBE_TIMEOUT_SEC) != SIDETNFS_PROBE_OK)
        return SIDETNFS_PROBE_TIMEOUT;
    *out_status = get32();
    return SIDETNFS_PROBE_OK;
}

int sidetnfs_probe_save_rtc_config(unsigned long *out_status)
{
    return simple(CMD_SAVE_RTC_CONFIG, 0, 0, out_status, SAVE_CONFIG_TIMEOUT_SEC);
}

int sidetnfs_probe_check_update(SideTnfsUpdateCheck *info)
{
    req_start(CMD_CHECK_UPDATE);
    if (transact(UPDATE_CHECK_TIMEOUT_SEC) != SIDETNFS_PROBE_OK)
        return SIDETNFS_PROBE_TIMEOUT;
    info->status = get32();
    getstr(info->latest_version,    SIDETNFS_UPDATE_VERSION_LEN);
    getstr(info->installed_version, SIDETNFS_UPDATE_VERSION_LEN);
    return SIDETNFS_PROBE_OK;
}

/* Sent exactly once, never retried (see sidetnfs_probe.c). */
int sidetnfs_probe_reboot_pico(void)
{
    req_start(CMD_REBOOT_PICO);
    return transact(PROBE_TIMEOUT_SEC);
}

static int get_status(unsigned long *rtc, unsigned long *link)
{
    req_start(CMD_GET_STATUS);
    if (transact(1) != SIDETNFS_PROBE_OK)
        return 0;
    *rtc = get32();
    *link = get32();
    return 1;
}

int sidetnfs_probe_get_rtc_sync_state(void)
{
    unsigned long rtc, link;
    if (!get_status(&rtc, &link))
        return SIDETNFS_RTC_SYNC_DISABLED;
    return (int)rtc;
}

int sidetnfs_probe_get_network_link_state(void)
{
    unsigned long rtc, link;
    if (!get_status(&rtc, &link))
        return SIDETNFS_NET_LINK_NOT_CONNECTED;
    return link ? SIDETNFS_NET_LINK_CONNECTED : SIDETNFS_NET_LINK_NOT_CONNECTED;
}
