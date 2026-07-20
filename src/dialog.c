/*
 * SideTNFS Configuration - GEM dialogs built in C, no .RSC file.
 *
 * Target : Atari ST / Mega STE, TOS 1.x / 2.x, 68000 CPU.
 * Minimum: 640x200 medium resolution.
 *
 * Fase AC-3: the main dialog is a drive overview (config drive + TNFS/SD
 * drives), each row opening its own small editor. The old single-profile
 * dialog and profile-list are gone; their fields/validation/TOUCHEXIT
 * idioms are reused in the new editors below.
 *
 * Conventions:
 *   - All declarations at top of each function (C89).
 *   - No dynamic memory.
 *   - All EXIT/TOUCHEXIT buttons use TOUCHEXIT for TOS 2.06 compatibility
 *     (TOS bug: plain EXIT buttons are not delivered while a text field is
 *     active; TOUCHEXIT fires on mouse-down before edit-mode routing).
 *   - After form_do() returns for a TOUCHEXIT button, poll graf_mkstate()
 *     until the mouse button is released before showing form_alert().
 */

#include <gem.h>
#include <mint/osbind.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dialog.h"
#include "drive.h"
#include "netconfig.h"
#include "sidetnfs_probe.h"

/* ================================================================== */
/* Config-drive-letter editor (CL_*)                                   */
/* ================================================================== */
enum {
    CL_ROOT = 0,
    CL_TITLE,
    CL_DIV1,
    CL_LBL_NICK, CL_VAL_NICK,
    CL_LBL_TYPE, CL_VAL_TYPE,
    CL_LBL_DRIVE, CL_DRIVE_EDIT,
    CL_DIV2,
    CL_OK, CL_CANCEL,
    CL_NOBJS
};

/* ================================================================== */
/* SD-drive editor (SD_*)                                              */
/* ================================================================== */
enum {
    SD_ROOT = 0,
    SD_TITLE,
    SD_DIV1,
    SD_LBL_DRIVE, SD_DRIVE_EDIT,
    SD_LBL_NICK,  SD_NICK_EDIT,
    SD_LBL_PATH,  SD_PATH_EDIT,
    SD_DIV2,
    SD_DELETE, SD_OK, SD_CANCEL,
    SD_NOBJS
};

/* ================================================================== */
/* TNFS-drive editor (TE_*)                                            */
/* ================================================================== */
enum {
    TE_ROOT = 0,
    TE_TITLE,
    TE_DIV1,
    TE_LBL_DRIVE, TE_DRIVE_EDIT,
    TE_LBL_NICK,  TE_NICK_EDIT,
    TE_LBL_HOST,  TE_HOST_EDIT,
    TE_LBL_PORT,  TE_PORT_EDIT,
    TE_LBL_TRANSPORT, TE_TRANSPORT_VAL, TE_TRANSPORT_TCP,
    TE_LBL_MOUNT, TE_MOUNT_EDIT, TE_MOUNT_HINT,
    TE_DIV2,
    TE_TEST, TE_DELETE, TE_OK, TE_CANCEL,
    TE_NOBJS
};

/* ================================================================== */
/* Add-disk type chooser (AD_*)                                        */
/* ================================================================== */
enum {
    AD_ROOT = 0,
    AD_TITLE,
    AD_DIV1,
    AD_TNFS, AD_SD, AD_CANCEL,
    AD_NOBJS
};

/* ================================================================== */
/* WiFi/network settings editor (NC_*)                                 */
/* Fase AC-5: local UI state only -- see netconfig.h. Country is a      */
/* plain two-letter code field, validated and uppercased on OK (see     */
/* nc_save_to_config()). DHCP/Static IP reuses the SELECTED-toggle       */
/* radio-pair idiom already proven by the old profile-list row          */
/* selection.                                                            */
/* ================================================================== */
enum {
    NC_ROOT = 0,
    NC_TITLE,
    NC_DIV1,
    NC_LBL_WIFI,
    NC_LBL_SSID,     NC_SSID_EDIT,
    NC_LBL_PASSWORD, NC_PASSWORD_EDIT,
    NC_LBL_AUTH,     NC_AUTH_VAL, NC_AUTH_BTN,
    NC_LBL_COUNTRY,  NC_COUNTRY_EDIT, NC_COUNTRY_HINT,
    NC_DIV2,
    NC_LBL_NETWORK,
    NC_DHCP_BTN, NC_STATIC_BTN,
    NC_LBL_IP,      NC_IP_EDIT,
    NC_LBL_NETMASK, NC_NETMASK_EDIT,
    NC_LBL_GATEWAY, NC_GATEWAY_EDIT,
    NC_LBL_DNS,     NC_DNS_EDIT,
    NC_DIV3,
    NC_OK, NC_CANCEL,
    NC_NOBJS
};

/* ================================================================== */
/* Drive overview / main dialog (OV_*)                                 */
/* ================================================================== */
enum {
    OV_ROOT = 0,
    OV_TITLE,
    OV_DIV1,
    OV_ROW_BASE
};
#define OV_ROW_TEXT(i) (OV_ROW_BASE + (i)*2 + 0)
#define OV_ROW_EDIT(i) (OV_ROW_BASE + (i)*2 + 1)
#define OV_AFTER_ROWS  (OV_ROW_BASE + MAX_DRIVES*2)
#define OV_DIV2   (OV_AFTER_ROWS + 0)
#define OV_ADD    (OV_AFTER_ROWS + 1)
#define OV_OK     (OV_AFTER_ROWS + 2)
#define OV_CANCEL (OV_AFTER_ROWS + 3)
#define OV_NOBJS  (OV_AFTER_ROWS + 4)

/* ================================================================== */
/* Status window (SW_*) -- Fase AC-6                                    */
/* The new top-level main window. The drive overview above (OV_*) is no */
/* longer shown directly -- it becomes drives_window_run()'s own modal  */
/* window, opened from here via the DRIVES button. All status text this */
/* phase is a static placeholder (see status_refresh_network()/          */
/* status_refresh_clock()); only "Active drives" reads real data, via   */
/* the existing DriveConfig model. Meaning is never color-only (plain   */
/* text + the existing thin-box dividers), so this is monochrome-safe.  */
/* ================================================================== */
enum {
    SW_ROOT = 0,
    SW_TITLE,
    SW_DIV1,
    SW_LBL_NETWORK,
    SW_NET_LINE_0, SW_NET_LINE_1, SW_NET_LINE_2, SW_NET_LINE_3,
    SW_DIV2,
    SW_LBL_CLOCK,
    SW_CLOCK_LINE_0, SW_CLOCK_LINE_1,
    SW_DIV3,
    SW_LBL_DRIVES,
    SW_DRIVE_LINE_BASE
};
#define SW_NUM_DRIVE_LINES   5
#define SW_DRIVE_LINE(i)     (SW_DRIVE_LINE_BASE + (i))
#define SW_AFTER_DRIVE_LINES (SW_DRIVE_LINE_BASE + SW_NUM_DRIVE_LINES)
#define SW_DIV4    (SW_AFTER_DRIVE_LINES + 0)
#define SW_CONFIG  (SW_AFTER_DRIVE_LINES + 1)
#define SW_DRIVES  (SW_AFTER_DRIVE_LINES + 2)
#define SW_SAVE    (SW_AFTER_DRIVE_LINES + 3)
#define SW_QUIT    (SW_AFTER_DRIVE_LINES + 4)
#define SW_NOBJS   (SW_AFTER_DRIVE_LINES + 5)

static OBJECT cl_dlg[CL_NOBJS];
static OBJECT sd_dlg[SD_NOBJS];
static OBJECT te_dlg[TE_NOBJS];
static OBJECT nc_dlg[NC_NOBJS];
static OBJECT ov_dlg[OV_NOBJS];
static OBJECT sw_dlg[SW_NOBJS];

/* ================================================================== */
/* Shared editable-field buffers/TEDINFOs                              */
/* Reused across CL/SD/TE editors (never open simultaneously). Sized to */
/* the firmware-agreed field lengths in drive.h.                       */
/* ================================================================== */
#define TE_BUF_DRV    3
#define TE_BUF_NICK   DRIVE_NICK_LEN   /* 24 */
#define TE_BUF_HOST   DRIVE_HOST_LEN   /* 64 */
#define TE_BUF_PORT   7
#define TE_BUF_MOUNT  DRIVE_MOUNT_LEN  /* 32 */
#define TE_BUF_SDPATH DRIVE_SDPATH_LEN /* 64, shares template with host */

static char buf_te_drive[TE_BUF_DRV];
static char buf_te_nick [TE_BUF_NICK];
static char buf_te_host [TE_BUF_HOST];
static char buf_te_port [TE_BUF_PORT];
static char buf_te_mount[TE_BUF_MOUNT];
static char buf_sd_path [TE_BUF_SDPATH];

static char tmpl_drv[TE_BUF_DRV],     vld_drv[TE_BUF_DRV];
static char tmpl_nick[TE_BUF_NICK],   vld_nick[TE_BUF_NICK];
static char tmpl_host[TE_BUF_HOST],   vld_host[TE_BUF_HOST];
static char tmpl_port[TE_BUF_PORT],   vld_port[TE_BUF_PORT];
static char tmpl_mount[TE_BUF_MOUNT], vld_mount[TE_BUF_MOUNT];

static TEDINFO ti_te_drive, ti_te_nick, ti_te_host, ti_te_port, ti_te_mount, ti_sd_path;

/* ================================================================== */
/* WiFi/network editable-field buffers/TEDINFOs (NC_*, Fase AC-5)      */
/* Distinct field sizes from the drive editors above -- netconfig.h    */
/* defines its own lengths, independent of drive.h.                    */
/* ================================================================== */
#define NC_BUF_SSID     NETCONFIG_SSID_LEN     /* 33 */
#define NC_BUF_PASSWORD NETCONFIG_PASSWORD_LEN /* 64 */
#define NC_BUF_COUNTRY  NETCONFIG_COUNTRY_LEN  /* 3: two letters + NUL */
#define NC_BUF_IPV4     NETCONFIG_IPV4_LEN     /* 16, shared by all four IPv4 fields */

static char buf_nc_ssid    [NC_BUF_SSID];
static char buf_nc_password[NC_BUF_PASSWORD];
static char buf_nc_country [NC_BUF_COUNTRY];
static char buf_nc_ip      [NC_BUF_IPV4];
static char buf_nc_netmask [NC_BUF_IPV4];
static char buf_nc_gateway [NC_BUF_IPV4];
static char buf_nc_dns     [NC_BUF_IPV4];

static char tmpl_nc_ssid[NC_BUF_SSID],         vld_nc_ssid[NC_BUF_SSID];
static char tmpl_nc_password[NC_BUF_PASSWORD], vld_nc_password[NC_BUF_PASSWORD];
static char tmpl_nc_country[NC_BUF_COUNTRY],   vld_nc_country[NC_BUF_COUNTRY];
static char tmpl_nc_ipv4[NC_BUF_IPV4],         vld_nc_ipv4[NC_BUF_IPV4];

static TEDINFO ti_nc_ssid, ti_nc_password, ti_nc_country, ti_nc_ip, ti_nc_netmask, ti_nc_gateway, ti_nc_dns;

/* Fase AC-4 (network protocol): four canonical auth-mode choices, cycled
 * by NC_AUTH_BTN the same way the earlier (now-removed) Country cycle
 * button worked. nc_auth_codes[] are the values actually written to the
 * wire when the user changes the selection; auth_mode_to_index() maps
 * the full 0-8 firmware range onto these four groups for display. */
static const char *nc_auth_names[] = { "Open", "WPA/TKIP", "WPA2/AES", "WPA2 Mixed" };
static const unsigned long nc_auth_codes[] = { 0UL, 1UL, 3UL, 6UL };
#define NC_AUTH_OPTION_COUNT ((int)(sizeof(nc_auth_codes) / sizeof(nc_auth_codes[0])))
#define NC_BUF_AUTH_VAL 12
static char buf_nc_auth_val[NC_BUF_AUTH_VAL];
static int nc_auth_index;             /* current display index while editing */
static int nc_auth_touched;           /* did the user click Change this session? */
static unsigned long nc_auth_original; /* raw value at load time, kept verbatim if untouched */

/* Fase AC-4 (network protocol): the password field shows this fixed
 * placeholder instead of the real value whenever one is already set --
 * see the report for why this, not per-keystroke masking, is what this
 * toolchain's TEDINFO can reliably do. If the buffer still equals this
 * placeholder at OK time, the existing password is kept untouched;
 * anything else typed replaces it. */
static const char NC_PASSWORD_PLACEHOLDER[] = "********";

static NetConfig g_netconfig;
/* Snapshot of the last known-good (loaded-from-firmware or
 * successfully-saved) network config, so Save can tell whether the user
 * actually changed anything and skip SET/SAVE_NETWORK_CONFIG entirely
 * when not -- see perform_save(). */
static NetConfig g_netconfig_baseline;

/* Remembers the outcome of the ONE-TIME network_startup_load() probe, so
 * status_refresh_network() can describe it without ever calling
 * sidetnfs_probe_get_network_config() again -- GET_NETWORK_CONFIG/0x0413
 * is only ever sent once, at startup. */
#define NETLOAD_OK          0 /* probe answered OK; g_netconfig is real firmware data */
#define NETLOAD_UNAVAILABLE 1 /* probe timed out -- no firmware / offline, same as the drive protocol's fallback */
#define NETLOAD_BAD_STATUS  2 /* probe answered but the firmware status was not OK */
static int g_netconfig_load_state = NETLOAD_UNAVAILABLE;
static unsigned long g_netconfig_load_fw_status = SIDETNFS_NETCONFIG_STATUS_OK; /* only meaningful when load_state == NETLOAD_BAD_STATUS */

/* ================================================================== */
/* Drive-overview row text buffers                                     */
/* ================================================================== */
#define OV_ROW_BUF 40
static char ov_row_text[MAX_DRIVES][OV_ROW_BUF];

/* ================================================================== */
/* Status window text buffers (Fase AC-6)                              */
/* ================================================================== */
#define SW_LINE_BUF 48
static char sw_net_line[4][SW_LINE_BUF];
static char sw_clock_line[2][SW_LINE_BUF];
static char sw_drive_line[SW_NUM_DRIVE_LINES][SW_LINE_BUF];

/* ================================================================== */
/* Shared helpers                                                      */
/* ================================================================== */

static void fill_n(char *s, char c, int n)
{
    int i;
    for (i = 0; i < n; i++) s[i] = c;
    s[n] = '\0';
}

/* Copy val into buf, NUL-terminate after actual content.
 * Do NOT space-pad: GEM places the edit cursor at strlen(te_ptext). */
static void set_buf(char *buf, int n, const char *val)
{
    int i;
    int vlen = (int)strlen(val);
    if (vlen > n - 1) vlen = n - 1;
    for (i = 0; i < vlen; i++) buf[i] = val[i];
    buf[vlen] = '\0';
}

static void init_ti(TEDINFO *ti,
                    char *text, char *tmpl, char *vld, int bufsz)
{
    ti->te_ptext     = text;
    ti->te_ptmplt    = tmpl;
    ti->te_pvalid    = vld;
    ti->te_font      = IBM;
    ti->te_fontid    = 0;
    ti->te_just      = TE_LEFT;
    ti->te_color     = 0x1100;
    ti->te_fontsize  = 0;
    ti->te_thickness = -1;
    ti->te_txtlen    = (short)bufsz;
    ti->te_tmplen    = (short)bufsz;
}

/* Assign geometry and attributes for one object in a given tree. */
static void set_obj(OBJECT *tree, int id, int type, int flags, int state,
                    int x, int y, int w, int h)
{
    tree[id].ob_type   = (unsigned short)type;
    tree[id].ob_flags  = (unsigned short)flags;
    tree[id].ob_state  = (unsigned short)state;
    tree[id].ob_x      = (short)x;
    tree[id].ob_y      = (short)y;
    tree[id].ob_width  = (short)w;
    tree[id].ob_height = (short)h;
}

/* Wire a flat tree: objects 1..n-1 are all direct children of root. */
static void wire_tree(OBJECT *tree, int n)
{
    int i;
    tree[0].ob_next = -1;
    tree[0].ob_head = 1;
    tree[0].ob_tail = n - 1;
    for (i = 1; i < n - 1; i++) {
        tree[i].ob_next = i + 1;
        tree[i].ob_head = -1;
        tree[i].ob_tail = -1;
    }
    tree[n-1].ob_next  = 0;
    tree[n-1].ob_head  = -1;
    tree[n-1].ob_tail  = -1;
    tree[n-1].ob_flags |= LASTOB;
}

/* Return 1 if buf contains at least one non-space, non-NUL character. */
static int buf_nonempty(const char *buf)
{
    const char *p;
    for (p = buf; *p; p++)
        if (*p != ' ') return 1;
    return 0;
}

/* Copy buf into dest (up to destlen-1 chars), stripping trailing spaces. */
static void buf_copy(const char *buf, char *dest, int destlen)
{
    int n;
    strncpy(dest, buf, destlen - 1);
    dest[destlen - 1] = '\0';
    n = (int)strlen(dest);
    while (n > 0 && dest[n-1] == ' ') dest[--n] = '\0';
}

/* Wait for left mouse button to be released (for use after TOUCHEXIT). */
static void wait_mouse_release(void)
{
    short mx, my, mb, mk;
    do { graf_mkstate(&mx, &my, &mb, &mk); } while (mb & 1);
}

static void shared_fields_init(void)
{
    static int ready = 0;
    if (ready) return;
    ready = 1;

    fill_n(tmpl_drv,   '_', TE_BUF_DRV   - 1); fill_n(vld_drv,   'X', TE_BUF_DRV   - 1);
    fill_n(tmpl_nick,  '_', TE_BUF_NICK  - 1); fill_n(vld_nick,  'X', TE_BUF_NICK  - 1);
    fill_n(tmpl_host,  '_', TE_BUF_HOST  - 1); fill_n(vld_host,  'X', TE_BUF_HOST  - 1);
    fill_n(tmpl_port,  '_', TE_BUF_PORT  - 1); fill_n(vld_port,  '9', TE_BUF_PORT  - 1);
    fill_n(tmpl_mount, '_', TE_BUF_MOUNT - 1); fill_n(vld_mount, 'X', TE_BUF_MOUNT - 1);

    init_ti(&ti_te_drive, buf_te_drive, tmpl_drv,   vld_drv,   TE_BUF_DRV);
    init_ti(&ti_te_nick,  buf_te_nick,  tmpl_nick,  vld_nick,  TE_BUF_NICK);
    init_ti(&ti_te_host,  buf_te_host,  tmpl_host,  vld_host,  TE_BUF_HOST);
    init_ti(&ti_te_port,  buf_te_port,  tmpl_port,  vld_port,  TE_BUF_PORT);
    init_ti(&ti_te_mount, buf_te_mount, tmpl_mount, vld_mount, TE_BUF_MOUNT);
    init_ti(&ti_sd_path,  buf_sd_path,  tmpl_host,  vld_host,  TE_BUF_SDPATH);

    /* Fase AC-5: plain free-text validation ('X'), same convention as
     * host/mount_path above -- no character-class masking for IPv4
     * octets or the password field, consistent with the rest of this
     * file and with "no extensive validation yet" for this phase. */
    fill_n(tmpl_nc_ssid,     '_', NC_BUF_SSID     - 1); fill_n(vld_nc_ssid,     'X', NC_BUF_SSID     - 1);
    fill_n(tmpl_nc_password, '_', NC_BUF_PASSWORD - 1); fill_n(vld_nc_password, 'X', NC_BUF_PASSWORD - 1);
    fill_n(tmpl_nc_country,  '_', NC_BUF_COUNTRY  - 1); fill_n(vld_nc_country,  'X', NC_BUF_COUNTRY  - 1);
    fill_n(tmpl_nc_ipv4,     '_', NC_BUF_IPV4     - 1); fill_n(vld_nc_ipv4,     'X', NC_BUF_IPV4     - 1);

    init_ti(&ti_nc_ssid,     buf_nc_ssid,     tmpl_nc_ssid,     vld_nc_ssid,     NC_BUF_SSID);
    init_ti(&ti_nc_password, buf_nc_password, tmpl_nc_password, vld_nc_password, NC_BUF_PASSWORD);
    init_ti(&ti_nc_country,  buf_nc_country,  tmpl_nc_country,  vld_nc_country,  NC_BUF_COUNTRY);
    init_ti(&ti_nc_ip,       buf_nc_ip,       tmpl_nc_ipv4,     vld_nc_ipv4,     NC_BUF_IPV4);
    init_ti(&ti_nc_netmask,  buf_nc_netmask,  tmpl_nc_ipv4,     vld_nc_ipv4,     NC_BUF_IPV4);
    init_ti(&ti_nc_gateway,  buf_nc_gateway,  tmpl_nc_ipv4,     vld_nc_ipv4,     NC_BUF_IPV4);
    init_ti(&ti_nc_dns,      buf_nc_dns,      tmpl_nc_ipv4,     vld_nc_ipv4,     NC_BUF_IPV4);

    netconfig_init_defaults(&g_netconfig);
}

static const char *drive_type_name(DriveType t)
{
    switch (t) {
    case DRIVE_TYPE_CONFIG: return "CONFIG";
    case DRIVE_TYPE_SD:     return "SD";
    case DRIVE_TYPE_TNFS:   return "TNFS";
    default:                return "?";
    }
}

/* ================================================================== */
/* Shared drive validation -- used both by each editor's Test/OK and by */
/* Save's full-list check. msg must be at least 100 bytes.             */
/* ================================================================== */
static int validate_drive(const Drive *d, const DriveConfig *cfg, int skip_index, char *msg)
{
    if (d->letter < 'A' || d->letter > 'Z' || d->letter == 'A' || d->letter == 'B') {
        sprintf(msg, "[3][Validation error|Drive %c: invalid letter.][OK]", d->letter);
        return 0;
    }
    if (drive_config_letter_in_use(cfg, d->letter, skip_index)) {
        sprintf(msg, "[3][Validation error|Drive %c: letter already used.][OK]", d->letter);
        return 0;
    }
    if (!buf_nonempty(d->nickname)) {
        sprintf(msg, "[3][Validation error|Drive %c: nickname is empty.][OK]", d->letter);
        return 0;
    }

    switch (d->type) {
    case DRIVE_TYPE_TNFS:
        if (d->transport != DRIVE_TRANSPORT_UDP) {
            sprintf(msg, "[3][Validation error|Drive %c: only UDP is supported.][OK]", d->letter);
            return 0;
        }
        if (!buf_nonempty(d->host)) {
            sprintf(msg, "[3][Validation error|Drive %c: host is empty.][OK]", d->letter);
            return 0;
        }
        if (d->port < 1 || d->port > 65535) {
            sprintf(msg, "[3][Validation error|Drive %c: port must be 1-65535.][OK]", d->letter);
            return 0;
        }
        /* Empty mount_path is valid: it means "server root". Not
         * normalized to "/" here -- the canonical empty-vs-"/"
         * representation is still being settled on the firmware side. */
        break;
    case DRIVE_TYPE_SD:
        if (!buf_nonempty(d->sd_path)) {
            sprintf(msg, "[3][Validation error|Drive %c: SD path is empty.][OK]", d->letter);
            return 0;
        }
        break;
    case DRIVE_TYPE_CONFIG:
    default:
        break;
    }
    return 1;
}

/* ================================================================== */
/* Firmware probe (Fase AC-4, protocol v2). Protocol v2 has no "active   */
/* server" concept -- it shows the GET_CONFIG_INFO summary, then the    */
/* record at a fixed representative slot 0 (the UI does not yet track a */
/* per-drive firmware slot index for an in-progress edit). Explicitly   */
/* not a network connection test of the drive being edited.             */
/* ================================================================== */
static void dialog_probe_firmware(void)
{
    SideTnfsConfigInfo config_info;
    SideTnfsDriveInfo drive_info;
    char msg[200];

    if (sidetnfs_probe_get_config_info(&config_info) != SIDETNFS_PROBE_OK) {
        form_alert(1, "[3][SideTNFS firmware|not responding (timeout).][OK]");
        return;
    }
    if (config_info.status != SIDETNFS_STATUS_OK) {
        sprintf(msg, "[3][SideTNFS firmware|Unexpected status %lu.][OK]", config_info.status);
        form_alert(1, msg);
        return;
    }
    if (config_info.protocol_version != 2) {
        sprintf(msg, "[3][Unexpected protocol version|Got %lu, expected 2.][OK]",
                config_info.protocol_version);
        form_alert(1, msg);
        return;
    }

    sprintf(msg, "[1][Firmware configuration|Config drive: %c:|Drives stored: %lu][OK]",
            (char)config_info.config_drive_letter, config_info.drive_count);
    form_alert(1, msg);

    if (sidetnfs_probe_get_drive(0, &drive_info) != SIDETNFS_PROBE_OK) {
        form_alert(1, "[3][SideTNFS drive|GET_DRIVE(0) timed out.][OK]");
        return;
    }

    switch (drive_info.status) {
    case SIDETNFS_STATUS_OK:
        if (drive_info.type == SIDETNFS_DRIVE_TYPE_TNFS) {
            sprintf(msg, "[1][Slot 0 (not a network test)|%c: %s|%s:%lu|%s / %s][OK]",
                    (char)drive_info.letter, drive_info.nickname,
                    drive_info.host, drive_info.port,
                    drive_info.transport == SIDETNFS_TRANSPORT_UDP ? "UDP" : "TCP",
                    drive_info.mount_path);
        } else if (drive_info.type == SIDETNFS_DRIVE_TYPE_SD) {
            sprintf(msg, "[1][Slot 0 (not a network test)|%c: %s|SD|%s][OK]",
                    (char)drive_info.letter, drive_info.nickname, drive_info.sd_path);
        } else {
            sprintf(msg, "[3][Slot 0|Unknown drive type %lu.][OK]", drive_info.type);
        }
        form_alert(1, msg);
        break;
    case SIDETNFS_STATUS_EMPTY_SLOT:
        form_alert(1, "[1][Slot 0|Firmware slot 0 is empty.][OK]");
        break;
    default:
        sprintf(msg, "[3][SideTNFS drive|Unexpected status %lu.][OK]", drive_info.status);
        form_alert(1, msg);
        break;
    }
}

/* ================================================================== */
/* Wire <-> UI drive translation (Fase AC-4)                            */
/* Explicit field-by-field translation, per the task's instruction to   */
/* never memcpy() between the wire struct and the UI struct (their      */
/* padding/alignment/field order are not proven identical, even though  */
/* several string lengths happen to match numerically today).           */
/* ================================================================== */

/* Caller must have already validated w->type is SD or TNFS -- an
 * unrecognized type is a load-time error, not something to silently
 * default to TNFS here. */
static void wire_to_ui_drive(const SideTnfsDriveInfo *w, Drive *d)
{
    memset(d, 0, sizeof(*d));
    d->used   = (int)w->used;
    d->letter = (char)w->letter;

    if (w->type == SIDETNFS_DRIVE_TYPE_TNFS) {
        d->type      = DRIVE_TYPE_TNFS;
        d->transport = (w->transport == SIDETNFS_TRANSPORT_TCP) ? DRIVE_TRANSPORT_TCP : DRIVE_TRANSPORT_UDP;
        d->port      = (int)w->port;
        strncpy(d->host, w->host, DRIVE_HOST_LEN - 1);
        d->host[DRIVE_HOST_LEN - 1] = '\0';
        strncpy(d->mount_path, w->mount_path, DRIVE_MOUNT_LEN - 1);
        d->mount_path[DRIVE_MOUNT_LEN - 1] = '\0';
    } else if (w->type == SIDETNFS_DRIVE_TYPE_SD) {
        d->type = DRIVE_TYPE_SD;
        strncpy(d->sd_path, w->sd_path, DRIVE_SDPATH_LEN - 1);
        d->sd_path[DRIVE_SDPATH_LEN - 1] = '\0';
    }

    strncpy(d->nickname, w->nickname, DRIVE_NICK_LEN - 1);
    d->nickname[DRIVE_NICK_LEN - 1] = '\0';
}

static void ui_to_wire_drive(const Drive *d, SideTnfsDriveInfo *w)
{
    memset(w, 0, sizeof(*w));
    w->used   = (unsigned long)(d->used ? 1 : 0);
    w->letter = (unsigned long)(unsigned char)d->letter;
    w->type   = (d->type == DRIVE_TYPE_SD)   ? SIDETNFS_DRIVE_TYPE_SD
              : (d->type == DRIVE_TYPE_TNFS) ? SIDETNFS_DRIVE_TYPE_TNFS
                                              : SIDETNFS_DRIVE_TYPE_NONE;
    w->transport = (d->transport == DRIVE_TRANSPORT_TCP) ? SIDETNFS_TRANSPORT_TCP : SIDETNFS_TRANSPORT_UDP;
    w->port      = (unsigned long)d->port;

    strncpy(w->nickname, d->nickname, SIDETNFS_NICKNAME_LEN - 1);
    if (d->type == DRIVE_TYPE_TNFS) {
        strncpy(w->host, d->host, SIDETNFS_HOST_LEN - 1);
        strncpy(w->mount_path, d->mount_path, SIDETNFS_MOUNTPATH_LEN - 1);
    } else if (d->type == DRIVE_TYPE_SD) {
        strncpy(w->sd_path, d->sd_path, SIDETNFS_SDPATH_LEN - 1);
    }
    /* NUL-termination of every field is already guaranteed by the
     * memset(w,0,...) above -- strncpy here never reaches the last byte
     * since the UI-side source fields are exactly one byte shorter. */
}

/* ================================================================== */
/* Firmware load / readback (Fase AC-4)                                 */
/* ================================================================== */

/* Builds *out entirely from firmware: GET_CONFIG_INFO, then GET_DRIVE
 * for every ordinary slot. Returns 1 on a fully consistent read, 0 on
 * any timeout, unexpected status, or unrecognized field -- *out is left
 * untouched on failure (never partially filled). No alerts: this is the
 * silent building block for both the startup load and the post-Save
 * readback verification, which report failure differently. */
static int fetch_drive_config_from_firmware(DriveConfig *out)
{
    SideTnfsConfigInfo config_info;
    SideTnfsDriveInfo wire;
    DriveConfig built;
    int i, n;
    char letter;

    if (sidetnfs_probe_get_config_info(&config_info) != SIDETNFS_PROBE_OK)
        return 0;
    if (config_info.status != SIDETNFS_STATUS_OK)
        return 0;
    if (config_info.protocol_version != 2)
        return 0;
    if (config_info.max_drives != (unsigned long)MAX_ORDINARY_DRIVES)
        return 0;

    letter = (char)config_info.config_drive_letter;
    if (letter < 'A' || letter > 'Z' || letter == 'A' || letter == 'B')
        return 0;

    memset(&built, 0, sizeof(built));
    built.drives[0].used   = 1;
    built.drives[0].letter = letter;
    built.drives[0].type   = DRIVE_TYPE_CONFIG;
    strncpy(built.drives[0].nickname, "Config disk", DRIVE_NICK_LEN - 1);
    n = 1;

    for (i = 0; i < MAX_ORDINARY_DRIVES; i++) {
        if (sidetnfs_probe_get_drive((unsigned long)i, &wire) != SIDETNFS_PROBE_OK)
            return 0;
        if (wire.status == SIDETNFS_STATUS_EMPTY_SLOT)
            continue; /* no UI row for this slot */
        if (wire.status != SIDETNFS_STATUS_OK)
            return 0;
        if (wire.type != SIDETNFS_DRIVE_TYPE_SD && wire.type != SIDETNFS_DRIVE_TYPE_TNFS)
            return 0; /* unknown/corrupt type -- never silently treated as TNFS */
        if (n >= MAX_DRIVES)
            return 0;

        wire_to_ui_drive(&wire, &built.drives[n]);
        n++;
    }

    built.drive_count = n;
    drive_config_sort_by_letter(&built);
    *out = built;
    return 1;
}

/* Startup wrapper: distinguishes "no firmware at all" (silent, expected
 * offline case -- a plain GET_CONFIG_INFO timeout) from "firmware
 * present but its configuration is unusable" (visible alert, since that
 * is not the normal offline case). On success, *cfg is entirely
 * firmware-driven; on failure *cfg keeps whatever main.c initialized it
 * to (built-in defaults, no file involved either way). */
static int dialog_startup_load(DriveConfig *cfg)
{
    SideTnfsConfigInfo probe;
    DriveConfig fw_cfg;

    if (sidetnfs_probe_get_config_info(&probe) != SIDETNFS_PROBE_OK)
        return 0; /* no firmware detected -- expected offline case, no alert */

    if (fetch_drive_config_from_firmware(&fw_cfg)) {
        *cfg = fw_cfg;
        return 1;
    }

    form_alert(1, "[3][SideTNFS firmware|Unexpected configuration.|Using local file instead.][OK]");
    return 0;
}

static int drives_match(const Drive *a, const Drive *b)
{
    if (a->used != b->used) return 0;
    if (a->letter != b->letter) return 0;
    if (a->type != b->type) return 0;
    if (strcmp(a->nickname, b->nickname) != 0) return 0;

    switch (a->type) {
    case DRIVE_TYPE_TNFS:
        if (a->transport != b->transport) return 0;
        if (a->port != b->port) return 0;
        if (strcmp(a->host, b->host) != 0) return 0;
        if (strcmp(a->mount_path, b->mount_path) != 0) return 0;
        break;
    case DRIVE_TYPE_SD:
        if (strcmp(a->sd_path, b->sd_path) != 0) return 0;
        break;
    case DRIVE_TYPE_CONFIG:
    default:
        break;
    }
    return 1;
}

/* Both configs must already be sorted by letter (drive_config_sort_by_letter
 * guarantees this for both the live UI list and fetch_drive_config_from_firmware's
 * output), so an index-by-index comparison after equal drive_count is valid. */
static int drive_config_matches(const DriveConfig *a, const DriveConfig *b)
{
    int i;
    if (a->drive_count != b->drive_count) return 0;
    for (i = 0; i < a->drive_count; i++)
        if (!drives_match(&a->drives[i], &b->drives[i]))
            return 0;
    return 1;
}

/* Fase AC-4: full firmware-backed Save. DELETE_DRIVE for all 8 ordinary
 * slots first (so letter swaps never hit a temporary duplicate), then
 * SET_CONFIG_DRIVE, one SET_DRIVE per used ordinary UI drive, then
 * exactly one SAVE_CONFIG, then a full readback + compare. Returns 1
 * only after the readback matches exactly; msg is always filled in on
 * failure. Never calls SAVE_CONFIG if an earlier step failed -- flash is
 * only ever touched by that one call. */
static int save_to_firmware(const DriveConfig *cfg, char *msg)
{
    unsigned long status;
    int i, slot, config_idx;
    unsigned long config_letter;
    DriveConfig readback;

    for (i = 0; i < MAX_ORDINARY_DRIVES; i++) {
        if (sidetnfs_probe_delete_drive((unsigned long)i, &status) != SIDETNFS_PROBE_OK) {
            sprintf(msg, "[3][Save to firmware failed|DELETE_DRIVE(%d) timed out.|Nothing was saved.][OK]", i);
            return 0;
        }
        if (status != SIDETNFS_STATUS_OK && status != SIDETNFS_STATUS_EMPTY_SLOT) {
            sprintf(msg, "[3][Save to firmware failed|DELETE_DRIVE(%d): status %lu.|Nothing was saved.][OK]", i, status);
            return 0;
        }
    }

    config_idx = drive_config_config_index(cfg);
    config_letter = (config_idx >= 0) ? (unsigned long)(unsigned char)cfg->drives[config_idx].letter : 0UL;
    if (sidetnfs_probe_set_config_drive(config_letter, &status) != SIDETNFS_PROBE_OK) {
        sprintf(msg, "[3][Save to firmware failed|SET_CONFIG_DRIVE timed out.|Nothing was saved.][OK]");
        return 0;
    }
    if (status != SIDETNFS_STATUS_OK) {
        sprintf(msg, "[3][Save to firmware failed|SET_CONFIG_DRIVE: status %lu.|Nothing was saved.][OK]", status);
        return 0;
    }

    slot = 0;
    for (i = 0; i < cfg->drive_count; i++) {
        SideTnfsDriveInfo wire;
        const Drive *d = &cfg->drives[i];

        if (d->type == DRIVE_TYPE_CONFIG)
            continue;
        if (slot >= MAX_ORDINARY_DRIVES) {
            sprintf(msg, "[3][Save to firmware failed|Too many drives.|Nothing was saved.][OK]");
            return 0;
        }

        ui_to_wire_drive(d, &wire);
        if (sidetnfs_probe_set_drive((unsigned long)slot, &wire, &status) != SIDETNFS_PROBE_OK) {
            sprintf(msg, "[3][Save to firmware failed|SET_DRIVE(%d) timed out.|Nothing was saved.][OK]", slot);
            return 0;
        }
        if (status != SIDETNFS_STATUS_OK) {
            sprintf(msg, "[3][Save to firmware failed|Drive %c: status %lu.|Nothing was saved.][OK]", d->letter, status);
            return 0;
        }
        slot++;
    }

    if (sidetnfs_probe_save_config(&status) != SIDETNFS_PROBE_OK) {
        sprintf(msg, "[3][Save to firmware failed|SAVE_CONFIG timed out.|Nothing was saved.][OK]");
        return 0;
    }
    if (status != SIDETNFS_STATUS_OK) {
        sprintf(msg, "[3][Save to firmware failed|SAVE_CONFIG: status %lu.][OK]", status);
        return 0;
    }

    if (!fetch_drive_config_from_firmware(&readback)) {
        sprintf(msg, "[3][Save verification failed|Could not read back|the saved configuration.][OK]");
        return 0;
    }
    if (!drive_config_matches(cfg, &readback)) {
        sprintf(msg, "[3][Save verification failed|Flash contents do not match|the edited list.][OK]");
        return 0;
    }

    return 1;
}

/* ================================================================== */
/* Wire <-> UI network-config translation (Fase AC-4, network protocol) */
/* Explicit field-by-field, same reasoning as wire_to_ui_drive() above:  */
/* never memcpy() between the wire struct and the UI struct. country is */
/* the one field with a real size difference (wire char[4] vs UI        */
/* char[3]/2 letters) -- both directions truncate/zero-pad safely via   */
/* strncpy + the destination's own prior memset/NUL.                    */
/* ================================================================== */

static void wire_to_ui_netconfig(const SideTnfsNetworkConfig *w, NetConfig *nc)
{
    memset(nc, 0, sizeof(*nc));
    nc->auth_mode = w->auth_mode;
    nc->ip_mode   = (w->use_dhcp != 0UL) ? NETCONFIG_MODE_DHCP : NETCONFIG_MODE_STATIC;

    strncpy(nc->ssid,       w->ssid,        NETCONFIG_SSID_LEN - 1);
    strncpy(nc->password,   w->password,    NETCONFIG_PASSWORD_LEN - 1);
    strncpy(nc->country,    w->country,     NETCONFIG_COUNTRY_LEN - 1); /* wire[4] -> UI[3]: first 2 letters */
    strncpy(nc->ip_address, w->ip_address,  NETCONFIG_IPV4_LEN - 1);
    strncpy(nc->netmask,    w->netmask,     NETCONFIG_IPV4_LEN - 1);
    strncpy(nc->gateway,    w->gateway,     NETCONFIG_IPV4_LEN - 1);
    strncpy(nc->dns_server, w->primary_dns, NETCONFIG_IPV4_LEN - 1);
}

static void ui_to_wire_netconfig(const NetConfig *nc, SideTnfsNetworkConfig *w)
{
    memset(w, 0, sizeof(*w));
    w->auth_mode = nc->auth_mode;
    w->use_dhcp  = (nc->ip_mode == NETCONFIG_MODE_DHCP) ? 1UL : 0UL;

    strncpy(w->ssid,        nc->ssid,       SIDETNFS_NET_SSID_LEN - 1);
    strncpy(w->password,    nc->password,   SIDETNFS_NET_PASSWORD_LEN - 1);
    strncpy(w->country,     nc->country,    SIDETNFS_NET_COUNTRY_LEN - 1); /* UI[3] -> wire[4]: rest stays NUL */
    strncpy(w->ip_address,  nc->ip_address, SIDETNFS_NET_IPV4_LEN - 1);
    strncpy(w->netmask,     nc->netmask,    SIDETNFS_NET_IPV4_LEN - 1);
    strncpy(w->gateway,     nc->gateway,    SIDETNFS_NET_IPV4_LEN - 1);
    strncpy(w->primary_dns, nc->dns_server, SIDETNFS_NET_IPV4_LEN - 1);
}

static const char *netconfig_status_text(unsigned long status)
{
    switch (status) {
    case SIDETNFS_NETCONFIG_STATUS_OK:                  return "OK.";
    case SIDETNFS_NETCONFIG_STATUS_INVALID_SSID:        return "Invalid SSID.";
    case SIDETNFS_NETCONFIG_STATUS_INVALID_PASSWORD:    return "Invalid password.";
    case SIDETNFS_NETCONFIG_STATUS_INVALID_AUTH_MODE:   return "Invalid authentication mode.";
    case SIDETNFS_NETCONFIG_STATUS_INVALID_COUNTRY:     return "Invalid country code.";
    case SIDETNFS_NETCONFIG_STATUS_INVALID_DHCP:        return "Invalid DHCP setting.";
    case SIDETNFS_NETCONFIG_STATUS_INVALID_IP:          return "Invalid IP address.";
    case SIDETNFS_NETCONFIG_STATUS_INVALID_NETMASK:     return "Invalid netmask.";
    case SIDETNFS_NETCONFIG_STATUS_INVALID_GATEWAY:     return "Invalid gateway.";
    case SIDETNFS_NETCONFIG_STATUS_INVALID_DNS:         return "Invalid DNS server.";
    case SIDETNFS_NETCONFIG_STATUS_NOT_STAGED:          return "No config staged.";
    case SIDETNFS_NETCONFIG_STATUS_FLASH_WRITE_FAILED:  return "Flash write failed.";
    case SIDETNFS_NETCONFIG_STATUS_FLASH_VERIFY_FAILED: return "Flash verify failed.";
    default:                                            return "Unknown status.";
    }
}

/* Silent building block, same shape as fetch_drive_config_from_firmware():
 * returns 1 on a fully consistent OK read, 0 on timeout or any non-OK
 * status -- *out is left untouched on failure. */
static int fetch_network_config_from_firmware(NetConfig *out)
{
    SideTnfsNetworkConfig wire;

    if (sidetnfs_probe_get_network_config(&wire) != SIDETNFS_PROBE_OK)
        return 0;
    if (wire.status != SIDETNFS_NETCONFIG_STATUS_OK)
        return 0;

    wire_to_ui_netconfig(&wire, out);
    return 1;
}

/* Field-by-field local validation, mirroring validate_drive()'s shape.
 * The country code itself is already enforced at OK time by
 * nc_save_to_config(), this is the same defensive re-check pattern
 * validate_drive() applies at Save time too. */
static int validate_netconfig(const NetConfig *nc, char *msg)
{
    if (!buf_nonempty(nc->ssid)) {
        sprintf(msg, "[3][Validation error|SSID is empty.][OK]");
        return 0;
    }
    if (strlen(nc->country) != 2) {
        sprintf(msg, "[3][Validation error|Country code must contain|exactly two letters.][OK]");
        return 0;
    }
    if (nc->ip_mode == NETCONFIG_MODE_STATIC) {
        if (!buf_nonempty(nc->ip_address)) {
            sprintf(msg, "[3][Validation error|Static IP: IP address is empty.][OK]");
            return 0;
        }
        if (!buf_nonempty(nc->netmask)) {
            sprintf(msg, "[3][Validation error|Static IP: netmask is empty.][OK]");
            return 0;
        }
        if (!buf_nonempty(nc->gateway)) {
            sprintf(msg, "[3][Validation error|Static IP: gateway is empty.][OK]");
            return 0;
        }
        if (!buf_nonempty(nc->dns_server)) {
            sprintf(msg, "[3][Validation error|Static IP: DNS server is empty.][OK]");
            return 0;
        }
    }
    return 1;
}

/* 1 if any field differs -- drives Save's "only save network config if
 * the user actually changed it" requirement. Deliberately compares the
 * real password (both sides hold it, masking is a display-only concern
 * in nc_load_from_config()/nc_save_to_config()). */
static int netconfig_changed(const NetConfig *a, const NetConfig *b)
{
    if (strcmp(a->ssid, b->ssid) != 0) return 1;
    if (strcmp(a->password, b->password) != 0) return 1;
    if (a->auth_mode != b->auth_mode) return 1;
    if (strcmp(a->country, b->country) != 0) return 1;
    if (a->ip_mode != b->ip_mode) return 1;
    if (strcmp(a->ip_address, b->ip_address) != 0) return 1;
    if (strcmp(a->netmask, b->netmask) != 0) return 1;
    if (strcmp(a->gateway, b->gateway) != 0) return 1;
    if (strcmp(a->dns_server, b->dns_server) != 0) return 1;
    return 0;
}

/* Fase AC-4 (network protocol): SET_NETWORK_CONFIG, then
 * SAVE_NETWORK_CONFIG, then a full GET_NETWORK_CONFIG readback +
 * compare -- same "stop at the first failure, never partially applied"
 * shape as save_to_firmware(). Never calls SAVE_NETWORK_CONFIG if
 * SET_NETWORK_CONFIG failed; on any failure *nc (the caller's live
 * g_netconfig) is never touched here, so a failed save cannot silently
 * replace the local UI values. */
static int save_network_to_firmware(const NetConfig *nc, char *msg)
{
    SideTnfsNetworkConfig wire;
    unsigned long status;
    NetConfig readback;

    ui_to_wire_netconfig(nc, &wire);

    if (sidetnfs_probe_set_network_config(&wire, &status) != SIDETNFS_PROBE_OK) {
        sprintf(msg, "[3][Network save failed|SET_NETWORK_CONFIG timed out.|Nothing was saved.][OK]");
        return 0;
    }
    if (status != SIDETNFS_NETCONFIG_STATUS_OK) {
        sprintf(msg, "[3][Network save failed|%s|Nothing was saved.][OK]", netconfig_status_text(status));
        return 0;
    }

    if (sidetnfs_probe_save_network_config(&status) != SIDETNFS_PROBE_OK) {
        sprintf(msg, "[3][Network save failed|SAVE_NETWORK_CONFIG timed out.|Nothing was saved.][OK]");
        return 0;
    }
    if (status != SIDETNFS_NETCONFIG_STATUS_OK) {
        sprintf(msg, "[3][Network save failed|%s][OK]", netconfig_status_text(status));
        return 0;
    }

    if (!fetch_network_config_from_firmware(&readback)) {
        sprintf(msg, "[3][Network save verification failed|Could not read back|the saved configuration.][OK]");
        return 0;
    }
    if (netconfig_changed(nc, &readback)) {
        sprintf(msg, "[3][Network save verification failed|Flash contents do not match|the edited settings.][OK]");
        return 0;
    }

    return 1;
}

/* Fase AC-4 (network protocol): startup counterpart to
 * dialog_startup_load(), called right after it regardless of whether a
 * drive-list firmware was found -- a timeout here gets the same silent
 * offline/fallback treatment (g_netconfig keeps its built-in defaults);
 * a reply with a non-OK status is visibly reported, since that is not
 * the normal offline case. Either way g_netconfig_baseline is set equal
 * to g_netconfig afterwards, so Save does not fire a spurious network
 * save for a session that never touched the Config dialog. */
static void network_startup_load(void)
{
    SideTnfsNetworkConfig wire;
    int result = sidetnfs_probe_get_network_config(&wire);

    if (result == SIDETNFS_PROBE_OK && wire.status == SIDETNFS_NETCONFIG_STATUS_OK) {
        wire_to_ui_netconfig(&wire, &g_netconfig);
        g_netconfig_load_state = NETLOAD_OK;
    } else if (result == SIDETNFS_PROBE_OK) {
        char msg[100];
        g_netconfig_load_state     = NETLOAD_BAD_STATUS;
        g_netconfig_load_fw_status = wire.status;
        sprintf(msg, "[3][SideTNFS network config|%s][OK]", netconfig_status_text(wire.status));
        form_alert(1, msg);
    } else {
        g_netconfig_load_state = NETLOAD_UNAVAILABLE;
    }
    g_netconfig_baseline = g_netconfig;
}

/* ================================================================== */
/* Config-drive-letter editor                                          */
/* Only the drive letter is editable; nickname/type are fixed context. */
/* No Test, no Delete -- the config drive can never be removed.        */
/* ================================================================== */

static void cl_dialog_init(void)
{
    short cw, ch, bw, bh;
    short sx, sy, sw, sh;
    int rh, tm, pitch;
    int DW, DH, xl, xf;
    int yt, ydiv1, ynick, ytype, ydrv, ydiv2, ybtn;

    graf_handle(&cw, &ch, &bw, &bh);
    wind_get(0, WF_WORKXYWH, &sx, &sy, &sw, &sh);
    if (sh <= 0 || sh > 800) sh = 200;

    rh    = (sh >= 350) ? (int)ch : ((ch > 8) ? ch / 2 : (int)ch);
    tm    = (rh / 4 > 2) ? rh / 4 : 2;
    pitch = rh + 3;

    DW = 40 * cw;
    xl = 2 * cw;
    xf = 13 * cw;

    yt    = tm;
    ydiv1 = yt + rh + 1;
    ynick = ydiv1 + 5;
    ytype = ynick + pitch;
    ydrv  = ytype + pitch;
    ydiv2 = ydrv + rh + 2;
    ybtn  = ydiv2 + 7;
    DH    = ybtn + rh + tm + 3;

    set_obj(cl_dlg, CL_ROOT, G_BOX, NONE, NORMAL, 0, 0, DW, DH);
    cl_dlg[CL_ROOT].ob_spec.index = 0x00031070L;

    set_obj(cl_dlg, CL_TITLE, G_STRING, NONE, NORMAL, 10*cw, yt, 20*cw, rh);
    cl_dlg[CL_TITLE].ob_spec.free_string = "Config Drive Letter";

    set_obj(cl_dlg, CL_DIV1, G_BOX, NONE, NORMAL, cw, ydiv1, DW - 2*cw, 2);
    cl_dlg[CL_DIV1].ob_spec.index = 0x00001171L;

    set_obj(cl_dlg, CL_LBL_NICK, G_STRING, NONE, NORMAL, xl, ynick, 11*cw, rh);
    cl_dlg[CL_LBL_NICK].ob_spec.free_string = "Nickname:";
    set_obj(cl_dlg, CL_VAL_NICK, G_STRING, NONE, NORMAL, xf, ynick, 23*cw, rh);
    cl_dlg[CL_VAL_NICK].ob_spec.free_string = "Config disk";

    set_obj(cl_dlg, CL_LBL_TYPE, G_STRING, NONE, NORMAL, xl, ytype, 11*cw, rh);
    cl_dlg[CL_LBL_TYPE].ob_spec.free_string = "Type:";
    set_obj(cl_dlg, CL_VAL_TYPE, G_STRING, NONE, NORMAL, xf, ytype, 10*cw, rh);
    cl_dlg[CL_VAL_TYPE].ob_spec.free_string = "CONFIG";

    set_obj(cl_dlg, CL_LBL_DRIVE, G_STRING, NONE, NORMAL, xl, ydrv, 11*cw, rh);
    cl_dlg[CL_LBL_DRIVE].ob_spec.free_string = "Drive:";
    set_obj(cl_dlg, CL_DRIVE_EDIT, G_FBOXTEXT, EDITABLE, NORMAL, xf, ydrv, 2*cw, rh);
    cl_dlg[CL_DRIVE_EDIT].ob_spec.tedinfo = &ti_te_drive;

    set_obj(cl_dlg, CL_DIV2, G_BOX, NONE, NORMAL, cw, ydiv2, DW - 2*cw, 2);
    cl_dlg[CL_DIV2].ob_spec.index = 0x00001171L;

    set_obj(cl_dlg, CL_OK, G_BUTTON, EXIT | DEFAULT | TOUCHEXIT, NORMAL, 11*cw, ybtn, 8*cw, rh);
    cl_dlg[CL_OK].ob_spec.free_string = "   OK   ";

    set_obj(cl_dlg, CL_CANCEL, G_BUTTON, EXIT | TOUCHEXIT, NORMAL, 22*cw, ybtn, 8*cw, rh);
    cl_dlg[CL_CANCEL].ob_spec.free_string = " Cancel ";

    wire_tree(cl_dlg, CL_NOBJS);

    (void)sx; (void)sy; (void)sw; (void)bw; (void)bh;
}

/* Applies the new letter to cfg->drives[index] on OK; leaves it
 * untouched on Cancel. */
static void cl_editor_run(DriveConfig *cfg, int index)
{
    short x, y, w, h;
    short which;
    int done;
    Drive *d = &cfg->drives[index];
    char msg[100];
    char newletter;
    char drv[2];

    cl_dialog_init();

    drv[0] = d->letter; drv[1] = '\0';
    set_buf(buf_te_drive, TE_BUF_DRV, drv);

    form_center(cl_dlg, &x, &y, &w, &h);
    form_dial(FMD_START, x, y, w, h, x, y, w, h);
    objc_draw(cl_dlg, CL_ROOT, MAX_DEPTH, x, y, w, h);

    done = 0;
    while (!done) {
        which = (short)(form_do(cl_dlg, CL_DRIVE_EDIT) & 0x7FFF);
        wait_mouse_release();

        switch (which) {
        case CL_OK:
            newletter = buf_te_drive[0];
            if (newletter >= 'a' && newletter <= 'z') newletter = (char)(newletter - 'a' + 'A');
            if (!drive_config_letter_valid(cfg, newletter, index)) {
                sprintf(msg, "[3][Validation error|Drive %c: invalid or already used.][OK]", newletter);
                form_alert(1, msg);
                break;
            }
            d->letter = newletter;
            done = 1;
            break;
        case CL_CANCEL:
        default:
            done = 1;
            break;
        }
    }

    form_dial(FMD_FINISH, x, y, w, h, x, y, w, h);
}

/* ================================================================== */
/* SD-drive editor                                                     */
/* ================================================================== */

static void sd_dialog_init(int show_delete)
{
    short cw, ch, bw, bh;
    short sx, sy, sw, sh;
    int rh, tm, pitch;
    int DW, DH, xl, xf;
    int yt, ydiv1, ydrv, ynick, ypath, ydiv2, ybtn;

    graf_handle(&cw, &ch, &bw, &bh);
    wind_get(0, WF_WORKXYWH, &sx, &sy, &sw, &sh);
    if (sh <= 0 || sh > 800) sh = 200;

    rh    = (sh >= 350) ? (int)ch : ((ch > 8) ? ch / 2 : (int)ch);
    tm    = (rh / 4 > 2) ? rh / 4 : 2;
    pitch = rh + 3;

    DW = 46 * cw;
    xl = 2 * cw;
    xf = 13 * cw;

    yt    = tm;
    ydiv1 = yt + rh + 1;
    ydrv  = ydiv1 + 5;
    ynick = ydrv + pitch;
    ypath = ynick + pitch;
    ydiv2 = ypath + rh + 2;
    ybtn  = ydiv2 + 7;
    DH    = ybtn + rh + tm + 3;

    set_obj(sd_dlg, SD_ROOT, G_BOX, NONE, NORMAL, 0, 0, DW, DH);
    sd_dlg[SD_ROOT].ob_spec.index = 0x00031070L;

    set_obj(sd_dlg, SD_TITLE, G_STRING, NONE, NORMAL, 17*cw, yt, 14*cw, rh);
    sd_dlg[SD_TITLE].ob_spec.free_string = "SD Drive";

    set_obj(sd_dlg, SD_DIV1, G_BOX, NONE, NORMAL, cw, ydiv1, DW - 2*cw, 2);
    sd_dlg[SD_DIV1].ob_spec.index = 0x00001171L;

    set_obj(sd_dlg, SD_LBL_DRIVE, G_STRING, NONE, NORMAL, xl, ydrv, 11*cw, rh);
    sd_dlg[SD_LBL_DRIVE].ob_spec.free_string = "Drive:";
    set_obj(sd_dlg, SD_DRIVE_EDIT, G_FBOXTEXT, EDITABLE, NORMAL, xf, ydrv, 2*cw, rh);
    sd_dlg[SD_DRIVE_EDIT].ob_spec.tedinfo = &ti_te_drive;

    set_obj(sd_dlg, SD_LBL_NICK, G_STRING, NONE, NORMAL, xl, ynick, 11*cw, rh);
    sd_dlg[SD_LBL_NICK].ob_spec.free_string = "Nickname:";
    set_obj(sd_dlg, SD_NICK_EDIT, G_FBOXTEXT, EDITABLE, NORMAL, xf, ynick, 23*cw, rh);
    sd_dlg[SD_NICK_EDIT].ob_spec.tedinfo = &ti_te_nick;

    set_obj(sd_dlg, SD_LBL_PATH, G_STRING, NONE, NORMAL, xl, ypath, 11*cw, rh);
    sd_dlg[SD_LBL_PATH].ob_spec.free_string = "SD path:";
    set_obj(sd_dlg, SD_PATH_EDIT, G_FBOXTEXT, EDITABLE, NORMAL, xf, ypath, 31*cw, rh);
    sd_dlg[SD_PATH_EDIT].ob_spec.tedinfo = &ti_sd_path;

    set_obj(sd_dlg, SD_DIV2, G_BOX, NONE, NORMAL, cw, ydiv2, DW - 2*cw, 2);
    sd_dlg[SD_DIV2].ob_spec.index = 0x00001171L;

    set_obj(sd_dlg, SD_DELETE, G_BUTTON, EXIT | TOUCHEXIT, show_delete ? NORMAL : DISABLED,
            4*cw, ybtn, 9*cw, rh);
    sd_dlg[SD_DELETE].ob_spec.free_string = " Delete  ";

    set_obj(sd_dlg, SD_OK, G_BUTTON, EXIT | DEFAULT | TOUCHEXIT, NORMAL, 17*cw, ybtn, 8*cw, rh);
    sd_dlg[SD_OK].ob_spec.free_string = "   OK   ";

    set_obj(sd_dlg, SD_CANCEL, G_BUTTON, EXIT | TOUCHEXIT, NORMAL, 28*cw, ybtn, 8*cw, rh);
    sd_dlg[SD_CANCEL].ob_spec.free_string = " Cancel ";

    wire_tree(sd_dlg, SD_NOBJS);

    (void)sx; (void)sy; (void)sw; (void)bw; (void)bh;
}

static void sd_load_from_drive(const Drive *d)
{
    char drv[2];
    drv[0] = d->letter; drv[1] = '\0';
    set_buf(buf_te_drive, TE_BUF_DRV, drv);
    set_buf(buf_te_nick,  TE_BUF_NICK, d->nickname);
    set_buf(buf_sd_path,  TE_BUF_SDPATH, d->sd_path);
}

static void sd_save_to_drive(Drive *d)
{
    char c = buf_te_drive[0];
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    d->letter = c;
    buf_copy(buf_te_nick, d->nickname, DRIVE_NICK_LEN);
    buf_copy(buf_sd_path, d->sd_path, DRIVE_SDPATH_LEN);
    d->type = DRIVE_TYPE_SD;
    d->used = 1;
}

/* Returns 2 if the drive was deleted, 1 if added/modified, 0 if
 * cancelled without changes. index < 0 means "add new". */
static int sd_editor_run(DriveConfig *cfg, int index)
{
    short x, y, w, h;
    short which;
    int done;
    int is_new = (index < 0);
    Drive working;
    char msg[100];

    if (is_new) {
        memset(&working, 0, sizeof(working));
        working.used   = 1;
        working.type   = DRIVE_TYPE_SD;
        working.letter = drive_config_suggest_letter(cfg);
    } else {
        working = cfg->drives[index];
    }

    sd_dialog_init(!is_new);
    sd_load_from_drive(&working);

    form_center(sd_dlg, &x, &y, &w, &h);
    form_dial(FMD_START, x, y, w, h, x, y, w, h);
    objc_draw(sd_dlg, SD_ROOT, MAX_DEPTH, x, y, w, h);

    done = 0;
    while (!done) {
        which = (short)(form_do(sd_dlg, SD_DRIVE_EDIT) & 0x7FFF);
        wait_mouse_release();

        switch (which) {
        case SD_DELETE:
            if (!is_new) {
                if (form_alert(1, "[2][Delete this drive?][Delete|Cancel]") == 1)
                    done = 2;
            }
            break;

        case SD_OK:
            sd_save_to_drive(&working);
            if (!validate_drive(&working, cfg, index, msg)) {
                form_alert(1, msg);
                break;
            }
            if (is_new) cfg->drives[cfg->drive_count++] = working;
            else        cfg->drives[index] = working;
            done = 1;
            break;

        case SD_CANCEL:
        default:
            done = 3;
            break;
        }
    }

    form_dial(FMD_FINISH, x, y, w, h, x, y, w, h);

    if (done == 2) return 2;
    if (done == 1) return 1;
    return 0;
}

/* ================================================================== */
/* TNFS-drive editor                                                    */
/* ================================================================== */

static void te_dialog_init(int show_delete)
{
    short cw, ch, bw, bh;
    short sx, sy, sw, sh;
    int rh, tm, pitch;
    int DW, DH, xl, xf;
    int yt, ydiv1, ydrv, ynick, yhost, yport, ytrans, ymount, ymounthint, ydiv2, ybtn;

    graf_handle(&cw, &ch, &bw, &bh);
    wind_get(0, WF_WORKXYWH, &sx, &sy, &sw, &sh);
    if (sh <= 0 || sh > 800) sh = 200;

    rh    = (sh >= 350) ? (int)ch : ((ch > 8) ? ch / 2 : (int)ch);
    tm    = (rh / 4 > 2) ? rh / 4 : 2;
    pitch = rh + 3;

    DW = 50 * cw;
    xl = 2 * cw;
    xf = 13 * cw;

    yt         = tm;
    ydiv1      = yt + rh + 1;
    ydrv       = ydiv1 + 5;
    ynick      = ydrv + pitch;
    yhost      = ynick + pitch;
    yport      = yhost + pitch;
    ytrans     = yport + pitch;
    ymount     = ytrans + pitch;
    ymounthint = ymount + pitch;
    ydiv2      = ymounthint + rh + 2;
    ybtn       = ydiv2 + 7;
    DH         = ybtn + rh + tm + 3;

    set_obj(te_dlg, TE_ROOT, G_BOX, NONE, NORMAL, 0, 0, DW, DH);
    te_dlg[TE_ROOT].ob_spec.index = 0x00031070L;

    set_obj(te_dlg, TE_TITLE, G_STRING, NONE, NORMAL, 18*cw, yt, 14*cw, rh);
    te_dlg[TE_TITLE].ob_spec.free_string = "TNFS Drive";

    set_obj(te_dlg, TE_DIV1, G_BOX, NONE, NORMAL, cw, ydiv1, DW - 2*cw, 2);
    te_dlg[TE_DIV1].ob_spec.index = 0x00001171L;

    set_obj(te_dlg, TE_LBL_DRIVE, G_STRING, NONE, NORMAL, xl, ydrv, 11*cw, rh);
    te_dlg[TE_LBL_DRIVE].ob_spec.free_string = "Drive:";
    set_obj(te_dlg, TE_DRIVE_EDIT, G_FBOXTEXT, EDITABLE, NORMAL, xf, ydrv, 2*cw, rh);
    te_dlg[TE_DRIVE_EDIT].ob_spec.tedinfo = &ti_te_drive;

    set_obj(te_dlg, TE_LBL_NICK, G_STRING, NONE, NORMAL, xl, ynick, 11*cw, rh);
    te_dlg[TE_LBL_NICK].ob_spec.free_string = "Nickname:";
    set_obj(te_dlg, TE_NICK_EDIT, G_FBOXTEXT, EDITABLE, NORMAL, xf, ynick, 23*cw, rh);
    te_dlg[TE_NICK_EDIT].ob_spec.tedinfo = &ti_te_nick;

    set_obj(te_dlg, TE_LBL_HOST, G_STRING, NONE, NORMAL, xl, yhost, 11*cw, rh);
    te_dlg[TE_LBL_HOST].ob_spec.free_string = "Host:";
    set_obj(te_dlg, TE_HOST_EDIT, G_FBOXTEXT, EDITABLE, NORMAL, xf, yhost, 31*cw, rh);
    te_dlg[TE_HOST_EDIT].ob_spec.tedinfo = &ti_te_host;

    set_obj(te_dlg, TE_LBL_PORT, G_STRING, NONE, NORMAL, xl, yport, 11*cw, rh);
    te_dlg[TE_LBL_PORT].ob_spec.free_string = "Port:";
    set_obj(te_dlg, TE_PORT_EDIT, G_FBOXTEXT, EDITABLE, NORMAL, xf, yport, 6*cw, rh);
    te_dlg[TE_PORT_EDIT].ob_spec.tedinfo = &ti_te_port;

    set_obj(te_dlg, TE_LBL_TRANSPORT, G_STRING, NONE, NORMAL, xl, ytrans, 11*cw, rh);
    te_dlg[TE_LBL_TRANSPORT].ob_spec.free_string = "Transport:";
    set_obj(te_dlg, TE_TRANSPORT_VAL, G_STRING, NONE, NORMAL, xf, ytrans, 5*cw, rh);
    te_dlg[TE_TRANSPORT_VAL].ob_spec.free_string = "UDP";
    /* TCP is visible but permanently DISABLED: GEM never delivers clicks
     * on a DISABLED object, which is the clearest available way to show
     * "not yet supported" without a click-then-reject handler. */
    set_obj(te_dlg, TE_TRANSPORT_TCP, G_BUTTON, NONE, DISABLED, xf + 6*cw, ytrans, 9*cw, rh);
    te_dlg[TE_TRANSPORT_TCP].ob_spec.free_string = "TCP (n/a)";

    set_obj(te_dlg, TE_LBL_MOUNT, G_STRING, NONE, NORMAL, xl, ymount, 11*cw, rh);
    te_dlg[TE_LBL_MOUNT].ob_spec.free_string = "Mount dir:";
    set_obj(te_dlg, TE_MOUNT_EDIT, G_FBOXTEXT, EDITABLE, NORMAL, xf, ymount, 23*cw, rh);
    te_dlg[TE_MOUNT_EDIT].ob_spec.tedinfo = &ti_te_mount;

    /* Empty mount_path is valid ("server root") -- not auto-normalized
     * to "/" yet, see validate_drive(). */
    set_obj(te_dlg, TE_MOUNT_HINT, G_STRING, NONE, NORMAL, xf, ymounthint, 30*cw, rh);
    te_dlg[TE_MOUNT_HINT].ob_spec.free_string = "empty = server root";

    set_obj(te_dlg, TE_DIV2, G_BOX, NONE, NORMAL, cw, ydiv2, DW - 2*cw, 2);
    te_dlg[TE_DIV2].ob_spec.index = 0x00001171L;

    set_obj(te_dlg, TE_TEST, G_BUTTON, EXIT | TOUCHEXIT, NORMAL, 2*cw, ybtn, 8*cw, rh);
    te_dlg[TE_TEST].ob_spec.free_string = "  Test  ";

    set_obj(te_dlg, TE_DELETE, G_BUTTON, EXIT | TOUCHEXIT, show_delete ? NORMAL : DISABLED,
            13*cw, ybtn, 9*cw, rh);
    te_dlg[TE_DELETE].ob_spec.free_string = " Delete  ";

    set_obj(te_dlg, TE_OK, G_BUTTON, EXIT | DEFAULT | TOUCHEXIT, NORMAL, 25*cw, ybtn, 8*cw, rh);
    te_dlg[TE_OK].ob_spec.free_string = "   OK   ";

    set_obj(te_dlg, TE_CANCEL, G_BUTTON, EXIT | TOUCHEXIT, NORMAL, 36*cw, ybtn, 8*cw, rh);
    te_dlg[TE_CANCEL].ob_spec.free_string = " Cancel ";

    wire_tree(te_dlg, TE_NOBJS);

    (void)sx; (void)sy; (void)sw; (void)bw; (void)bh;
}

static void te_load_from_drive(const Drive *d)
{
    char drv[2];
    char port_str[TE_BUF_PORT];

    drv[0] = d->letter; drv[1] = '\0';
    set_buf(buf_te_drive, TE_BUF_DRV, drv);
    set_buf(buf_te_nick,  TE_BUF_NICK, d->nickname);
    set_buf(buf_te_host,  TE_BUF_HOST, d->host);
    sprintf(port_str, "%d", d->port);
    set_buf(buf_te_port,  TE_BUF_PORT, port_str);
    set_buf(buf_te_mount, TE_BUF_MOUNT, d->mount_path);
}

static void te_save_to_drive(Drive *d)
{
    char c;

    c = buf_te_drive[0];
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    d->letter = c;

    buf_copy(buf_te_nick,  d->nickname,   DRIVE_NICK_LEN);
    buf_copy(buf_te_host,  d->host,       DRIVE_HOST_LEN);
    buf_copy(buf_te_mount, d->mount_path, DRIVE_MOUNT_LEN);

    /* A field left empty by the user means "server root": silently
     * normalized to "/" here rather than sent to the firmware as "". */
    if (d->mount_path[0] == '\0') {
        d->mount_path[0] = '/';
        d->mount_path[1] = '\0';
    }

    d->port = atoi(buf_te_port); /* range-checked by validate_drive() */
    d->transport = DRIVE_TRANSPORT_UDP; /* TCP button is DISABLED: never settable this phase */
    d->type = DRIVE_TYPE_TNFS;
    d->used = 1;
}

/* Returns 2 if the drive was deleted, 1 if added/modified, 0 if
 * cancelled without changes. index < 0 means "add new". */
static int tnfs_editor_run(DriveConfig *cfg, int index)
{
    short x, y, w, h;
    short which;
    int done;
    int is_new = (index < 0);
    Drive working;
    char msg[220];

    if (is_new) {
        memset(&working, 0, sizeof(working));
        working.used      = 1;
        working.type      = DRIVE_TYPE_TNFS;
        working.letter    = drive_config_suggest_letter(cfg);
        working.transport = DRIVE_TRANSPORT_UDP;
        working.port      = 16384;
        working.mount_path[0] = '/';
        working.mount_path[1] = '\0';
    } else {
        working = cfg->drives[index];
    }

    te_dialog_init(!is_new);
    te_load_from_drive(&working);

    form_center(te_dlg, &x, &y, &w, &h);
    form_dial(FMD_START, x, y, w, h, x, y, w, h);
    objc_draw(te_dlg, TE_ROOT, MAX_DEPTH, x, y, w, h);

    done = 0;
    while (!done) {
        which = (short)(form_do(te_dlg, TE_DRIVE_EDIT) & 0x7FFF);
        wait_mouse_release();

        switch (which) {
        case TE_TEST:
            te_dlg[TE_TEST].ob_state |= (unsigned short)SELECTED;
            objc_draw(te_dlg, TE_TEST, MAX_DEPTH, x, y, w, h);
            te_save_to_drive(&working);
            if (validate_drive(&working, cfg, index, msg))
                dialog_probe_firmware();
            else
                form_alert(1, msg);
            te_dlg[TE_TEST].ob_state &= (unsigned short)(~SELECTED);
            objc_draw(te_dlg, TE_TEST, MAX_DEPTH, x, y, w, h);
            break;

        case TE_DELETE:
            if (!is_new) {
                if (form_alert(1, "[2][Delete this drive?][Delete|Cancel]") == 1)
                    done = 2;
            }
            break;

        case TE_OK:
            te_save_to_drive(&working);
            if (!validate_drive(&working, cfg, index, msg)) {
                form_alert(1, msg);
                break;
            }
            if (is_new) cfg->drives[cfg->drive_count++] = working;
            else        cfg->drives[index] = working;
            done = 1;
            break;

        case TE_CANCEL:
        default:
            done = 3;
            break;
        }
    }

    form_dial(FMD_FINISH, x, y, w, h, x, y, w, h);

    if (done == 2) return 2;
    if (done == 1) return 1;
    return 0;
}

/* ================================================================== */
/* Add-disk type chooser                                               */
/* ================================================================== */

/* Returns DRIVE_TYPE_TNFS, DRIVE_TYPE_SD, or -1 if cancelled. */
static int add_disk_type_run(void)
{
    static OBJECT ad_dlg[AD_NOBJS];
    short cw, ch, bw, bh;
    short sx, sy, sw, sh;
    int rh, tm;
    int DW, DH;
    int yt, ydiv1, ybtn;
    short x, y, w, h;
    short which;

    graf_handle(&cw, &ch, &bw, &bh);
    wind_get(0, WF_WORKXYWH, &sx, &sy, &sw, &sh);
    if (sh <= 0 || sh > 800) sh = 200;

    rh = (sh >= 350) ? (int)ch : ((ch > 8) ? ch / 2 : (int)ch);
    tm = (rh / 4 > 2) ? rh / 4 : 2;

    DW    = 30 * cw;
    yt    = tm;
    ydiv1 = yt + rh + 1;
    ybtn  = ydiv1 + 7;
    DH    = ybtn + rh + tm + 3;

    set_obj(ad_dlg, AD_ROOT, G_BOX, NONE, NORMAL, 0, 0, DW, DH);
    ad_dlg[AD_ROOT].ob_spec.index = 0x00031070L;

    set_obj(ad_dlg, AD_TITLE, G_STRING, NONE, NORMAL, 8*cw, yt, 14*cw, rh);
    ad_dlg[AD_TITLE].ob_spec.free_string = "Add disk";

    set_obj(ad_dlg, AD_DIV1, G_BOX, NONE, NORMAL, cw, ydiv1, DW - 2*cw, 2);
    ad_dlg[AD_DIV1].ob_spec.index = 0x00001171L;

    set_obj(ad_dlg, AD_TNFS, G_BUTTON, EXIT | TOUCHEXIT, NORMAL, 2*cw, ybtn, 8*cw, rh);
    ad_dlg[AD_TNFS].ob_spec.free_string = "  TNFS  ";

    set_obj(ad_dlg, AD_SD, G_BUTTON, EXIT | TOUCHEXIT, NORMAL, 11*cw, ybtn, 8*cw, rh);
    ad_dlg[AD_SD].ob_spec.free_string = "   SD   ";

    set_obj(ad_dlg, AD_CANCEL, G_BUTTON, EXIT | TOUCHEXIT, NORMAL, 20*cw, ybtn, 8*cw, rh);
    ad_dlg[AD_CANCEL].ob_spec.free_string = " Cancel ";

    wire_tree(ad_dlg, AD_NOBJS);

    form_center(ad_dlg, &x, &y, &w, &h);
    form_dial(FMD_START, x, y, w, h, x, y, w, h);
    objc_draw(ad_dlg, AD_ROOT, MAX_DEPTH, x, y, w, h);

    which = (short)(form_do(ad_dlg, AD_ROOT) & 0x7FFF);
    wait_mouse_release();

    form_dial(FMD_FINISH, x, y, w, h, x, y, w, h);

    (void)sx; (void)sy; (void)sw; (void)bw; (void)bh;

    if (which == AD_TNFS) return DRIVE_TYPE_TNFS;
    if (which == AD_SD)   return DRIVE_TYPE_SD;
    return -1;
}

/* ================================================================== */
/* WiFi/network settings editor (Fase AC-5)                            */
/* UI only: no firmware read/write, no flash, no file. OK copies the   */
/* edit buffers into *nc; Cancel (or ESC, see netconfig_editor_run())  */
/* leaves *nc untouched -- same "temporary working buffers, committed  */
/* only on OK" idiom as the drive editors above.                       */
/* ================================================================== */

static void nc_dialog_init(void)
{
    short cw, ch, bw, bh;
    short sx, sy, sw, sh;
    int rh, tm, pitch;
    int DW, DH, xl, xf;
    int yt, ydiv1, ylblwifi, yssid, ypassword, yauth, ycountry, ydiv2,
        ylblnet, ymode, yip, ynetmask, ygateway, ydns, ydiv3, ybtn;

    graf_handle(&cw, &ch, &bw, &bh);
    wind_get(0, WF_WORKXYWH, &sx, &sy, &sw, &sh);
    if (sh <= 0 || sh > 800) sh = 200;

    rh    = (sh >= 350) ? (int)ch : ((ch > 8) ? ch / 2 : (int)ch);
    tm    = (rh / 4 > 2) ? rh / 4 : 2;
    pitch = rh + 3;

    DW = 50 * cw;
    xl = 2 * cw;
    xf = 13 * cw;

    yt        = tm;
    ydiv1     = yt + rh + 1;
    ylblwifi  = ydiv1 + 5;
    yssid     = ylblwifi + pitch;
    ypassword = yssid + pitch;
    yauth     = ypassword + pitch;
    ycountry  = yauth + pitch;
    ydiv2     = ycountry + rh + 2;
    ylblnet   = ydiv2 + 2;
    ymode     = ylblnet + pitch;
    yip       = ymode + pitch;
    ynetmask  = yip + pitch;
    ygateway  = ynetmask + pitch;
    ydns      = ygateway + pitch;
    ydiv3     = ydns + rh + 2;
    ybtn      = ydiv3 + 7;
    DH        = ybtn + rh + tm + 3;

    set_obj(nc_dlg, NC_ROOT, G_BOX, NONE, NORMAL, 0, 0, DW, DH);
    nc_dlg[NC_ROOT].ob_spec.index = 0x00031070L;

    set_obj(nc_dlg, NC_TITLE, G_STRING, NONE, NORMAL, 15*cw, yt, 20*cw, rh);
    nc_dlg[NC_TITLE].ob_spec.free_string = "Network Settings";

    set_obj(nc_dlg, NC_DIV1, G_BOX, NONE, NORMAL, cw, ydiv1, DW - 2*cw, 2);
    nc_dlg[NC_DIV1].ob_spec.index = 0x00001171L;

    set_obj(nc_dlg, NC_LBL_WIFI, G_STRING, NONE, NORMAL, xl, ylblwifi, 10*cw, rh);
    nc_dlg[NC_LBL_WIFI].ob_spec.free_string = "WiFi";

    set_obj(nc_dlg, NC_LBL_SSID, G_STRING, NONE, NORMAL, xl, yssid, 11*cw, rh);
    nc_dlg[NC_LBL_SSID].ob_spec.free_string = "SSID:";
    set_obj(nc_dlg, NC_SSID_EDIT, G_FBOXTEXT, EDITABLE, NORMAL, xf, yssid, 23*cw, rh);
    nc_dlg[NC_SSID_EDIT].ob_spec.tedinfo = &ti_nc_ssid;

    set_obj(nc_dlg, NC_LBL_PASSWORD, G_STRING, NONE, NORMAL, xl, ypassword, 11*cw, rh);
    nc_dlg[NC_LBL_PASSWORD].ob_spec.free_string = "Password:";
    set_obj(nc_dlg, NC_PASSWORD_EDIT, G_FBOXTEXT, EDITABLE, NORMAL, xf, ypassword, 23*cw, rh);
    nc_dlg[NC_PASSWORD_EDIT].ob_spec.tedinfo = &ti_nc_password;

    set_obj(nc_dlg, NC_LBL_AUTH, G_STRING, NONE, NORMAL, xl, yauth, 11*cw, rh);
    nc_dlg[NC_LBL_AUTH].ob_spec.free_string = "Auth:";
    set_obj(nc_dlg, NC_AUTH_VAL, G_STRING, NONE, NORMAL, xf, yauth, 11*cw, rh);
    nc_dlg[NC_AUTH_VAL].ob_spec.free_string = buf_nc_auth_val;
    set_obj(nc_dlg, NC_AUTH_BTN, G_BUTTON, EXIT | TOUCHEXIT, NORMAL, xf + 12*cw, yauth, 9*cw, rh);
    nc_dlg[NC_AUTH_BTN].ob_spec.free_string = " Change  ";

    set_obj(nc_dlg, NC_LBL_COUNTRY, G_STRING, NONE, NORMAL, xl, ycountry, 11*cw, rh);
    nc_dlg[NC_LBL_COUNTRY].ob_spec.free_string = "Country:";
    set_obj(nc_dlg, NC_COUNTRY_EDIT, G_FBOXTEXT, EDITABLE, NORMAL, xf, ycountry, 2*cw, rh);
    nc_dlg[NC_COUNTRY_EDIT].ob_spec.tedinfo = &ti_nc_country;
    set_obj(nc_dlg, NC_COUNTRY_HINT, G_STRING, NONE, NORMAL, xf + 3*cw, ycountry, 20*cw, rh);
    nc_dlg[NC_COUNTRY_HINT].ob_spec.free_string = "(XX for worldwide)";

    set_obj(nc_dlg, NC_DIV2, G_BOX, NONE, NORMAL, cw, ydiv2, DW - 2*cw, 2);
    nc_dlg[NC_DIV2].ob_spec.index = 0x00001171L;

    set_obj(nc_dlg, NC_LBL_NETWORK, G_STRING, NONE, NORMAL, xl, ylblnet, 10*cw, rh);
    nc_dlg[NC_LBL_NETWORK].ob_spec.free_string = "Network";

    /* DHCP/Static IP: manual mutually-exclusive SELECTED pair, same
     * EXIT|TOUCHEXIT-plus-manual-ob_state technique the old profile-list
     * row selection used -- no SELECTABLE flag, since that would let GEM
     * auto-toggle the bit too and race with the manual handling below. */
    set_obj(nc_dlg, NC_DHCP_BTN, G_BUTTON, EXIT | TOUCHEXIT, NORMAL, xf, ymode, 8*cw, rh);
    nc_dlg[NC_DHCP_BTN].ob_spec.free_string = "  DHCP  ";
    set_obj(nc_dlg, NC_STATIC_BTN, G_BUTTON, EXIT | TOUCHEXIT, NORMAL, xf + 9*cw, ymode, 12*cw, rh);
    nc_dlg[NC_STATIC_BTN].ob_spec.free_string = " Static IP  ";

    set_obj(nc_dlg, NC_LBL_IP, G_STRING, NONE, NORMAL, xl, yip, 11*cw, rh);
    nc_dlg[NC_LBL_IP].ob_spec.free_string = "IP address:";
    set_obj(nc_dlg, NC_IP_EDIT, G_FBOXTEXT, EDITABLE, NORMAL, xf, yip, 15*cw, rh);
    nc_dlg[NC_IP_EDIT].ob_spec.tedinfo = &ti_nc_ip;

    set_obj(nc_dlg, NC_LBL_NETMASK, G_STRING, NONE, NORMAL, xl, ynetmask, 11*cw, rh);
    nc_dlg[NC_LBL_NETMASK].ob_spec.free_string = "Netmask:";
    set_obj(nc_dlg, NC_NETMASK_EDIT, G_FBOXTEXT, EDITABLE, NORMAL, xf, ynetmask, 15*cw, rh);
    nc_dlg[NC_NETMASK_EDIT].ob_spec.tedinfo = &ti_nc_netmask;

    set_obj(nc_dlg, NC_LBL_GATEWAY, G_STRING, NONE, NORMAL, xl, ygateway, 11*cw, rh);
    nc_dlg[NC_LBL_GATEWAY].ob_spec.free_string = "Gateway:";
    set_obj(nc_dlg, NC_GATEWAY_EDIT, G_FBOXTEXT, EDITABLE, NORMAL, xf, ygateway, 15*cw, rh);
    nc_dlg[NC_GATEWAY_EDIT].ob_spec.tedinfo = &ti_nc_gateway;

    set_obj(nc_dlg, NC_LBL_DNS, G_STRING, NONE, NORMAL, xl, ydns, 11*cw, rh);
    nc_dlg[NC_LBL_DNS].ob_spec.free_string = "DNS server:";
    set_obj(nc_dlg, NC_DNS_EDIT, G_FBOXTEXT, EDITABLE, NORMAL, xf, ydns, 15*cw, rh);
    nc_dlg[NC_DNS_EDIT].ob_spec.tedinfo = &ti_nc_dns;

    set_obj(nc_dlg, NC_DIV3, G_BOX, NONE, NORMAL, cw, ydiv3, DW - 2*cw, 2);
    nc_dlg[NC_DIV3].ob_spec.index = 0x00001171L;

    set_obj(nc_dlg, NC_OK, G_BUTTON, EXIT | DEFAULT | TOUCHEXIT, NORMAL, 15*cw, ybtn, 8*cw, rh);
    nc_dlg[NC_OK].ob_spec.free_string = "   OK   ";

    set_obj(nc_dlg, NC_CANCEL, G_BUTTON, EXIT | TOUCHEXIT, NORMAL, 26*cw, ybtn, 8*cw, rh);
    nc_dlg[NC_CANCEL].ob_spec.free_string = " Cancel ";

    wire_tree(nc_dlg, NC_NOBJS);

    (void)sx; (void)sy; (void)sw; (void)bw; (void)bh;
}

/* Enables/disables the four IPv4 fields to match the selected mode --
 * DISABLED objects are both greyed out by objc_draw() and skipped by
 * form_do(), so this alone satisfies "visibly unavailable" and
 * "not editable" at once. */
static void nc_apply_ip_field_state(int ip_mode)
{
    if (ip_mode == NETCONFIG_MODE_STATIC) {
        nc_dlg[NC_IP_EDIT].ob_state      &= (unsigned short)(~DISABLED);
        nc_dlg[NC_NETMASK_EDIT].ob_state &= (unsigned short)(~DISABLED);
        nc_dlg[NC_GATEWAY_EDIT].ob_state &= (unsigned short)(~DISABLED);
        nc_dlg[NC_DNS_EDIT].ob_state     &= (unsigned short)(~DISABLED);
    } else {
        nc_dlg[NC_IP_EDIT].ob_state      |= (unsigned short)DISABLED;
        nc_dlg[NC_NETMASK_EDIT].ob_state |= (unsigned short)DISABLED;
        nc_dlg[NC_GATEWAY_EDIT].ob_state |= (unsigned short)DISABLED;
        nc_dlg[NC_DNS_EDIT].ob_state     |= (unsigned short)DISABLED;
    }
}

/* Maps the full 0-8 firmware auth_mode range onto the four canonical UI
 * groups (see nc_auth_names[]/nc_auth_codes[]): 0=Open, 1-2=WPA/TKIP,
 * 3-5=WPA2/AES, 6-8=WPA2 Mixed. Anything else (should not happen; the
 * firmware validates auth_mode <= 8) falls back to WPA2/AES. */
static int auth_mode_to_index(unsigned long auth_mode)
{
    if (auth_mode == 0UL) return 0;
    if (auth_mode == 1UL || auth_mode == 2UL) return 1;
    if (auth_mode >= 3UL && auth_mode <= 5UL) return 2;
    if (auth_mode >= 6UL && auth_mode <= 8UL) return 3;
    return 2;
}

static void nc_load_from_config(const NetConfig *nc)
{
    set_buf(buf_nc_ssid, NC_BUF_SSID, nc->ssid);

    /* Password masking: this toolchain's TEDINFO has no confirmed
     * per-keystroke masking (see report), so a fixed placeholder is
     * shown instead of the real value whenever one is already set; an
     * empty stored password shows an empty field. nc_save_to_config()
     * only overwrites nc->password if the buffer no longer equals this
     * exact placeholder at OK time. */
    if (nc->password[0] != '\0')
        set_buf(buf_nc_password, NC_BUF_PASSWORD, NC_PASSWORD_PLACEHOLDER);
    else
        buf_nc_password[0] = '\0';

    nc_auth_original = nc->auth_mode;
    nc_auth_touched   = 0;
    nc_auth_index     = auth_mode_to_index(nc->auth_mode);
    /* Space-padded to the full field width: G_STRING objects do not
     * clear their background before drawing, so a shorter new value
     * would leave trailing characters of a longer previous one behind. */
    sprintf(buf_nc_auth_val, "%-11.11s", nc_auth_names[nc_auth_index]);

    set_buf(buf_nc_country,  NC_BUF_COUNTRY,  nc->country);
    set_buf(buf_nc_ip,       NC_BUF_IPV4,     nc->ip_address);
    set_buf(buf_nc_netmask,  NC_BUF_IPV4,     nc->netmask);
    set_buf(buf_nc_gateway,  NC_BUF_IPV4,     nc->gateway);
    set_buf(buf_nc_dns,      NC_BUF_IPV4,     nc->dns_server);

    if (nc->ip_mode == NETCONFIG_MODE_STATIC) {
        nc_dlg[NC_STATIC_BTN].ob_state |= (unsigned short)SELECTED;
        nc_dlg[NC_DHCP_BTN].ob_state   &= (unsigned short)(~SELECTED);
    } else {
        nc_dlg[NC_DHCP_BTN].ob_state   |= (unsigned short)SELECTED;
        nc_dlg[NC_STATIC_BTN].ob_state &= (unsigned short)(~SELECTED);
    }
    nc_apply_ip_field_state(nc->ip_mode);
}

/* Returns 1 on success (fields copied into *nc), 0 if the country code
 * is invalid -- *nc is left untouched on failure, same "reject and stay
 * open" pattern as validate_drive() elsewhere in this file. */
static int nc_save_to_config(NetConfig *nc)
{
    char code[NC_BUF_COUNTRY];
    int len;

    /* buf_copy() strips the trailing spaces GEM pads editable fields
     * with, same as every other text field in this file. */
    buf_copy(buf_nc_country, code, sizeof(code));
    len = (int)strlen(code);
    if (len == 2 && code[0] >= 'a' && code[0] <= 'z') code[0] = (char)(code[0] - 'a' + 'A');
    if (len == 2 && code[1] >= 'a' && code[1] <= 'z') code[1] = (char)(code[1] - 'a' + 'A');

    if (len != 2 || code[0] < 'A' || code[0] > 'Z' || code[1] < 'A' || code[1] > 'Z') {
        form_alert(1, "[3][Validation error|Country code must contain|exactly two letters.][OK]");
        return 0;
    }

    buf_copy(buf_nc_ssid,     nc->ssid,       NETCONFIG_SSID_LEN);

    /* Only replace the stored password if the field no longer reads as
     * the untouched placeholder -- see nc_load_from_config(). */
    {
        char pw[NC_BUF_PASSWORD];
        buf_copy(buf_nc_password, pw, sizeof(pw));
        if (strcmp(pw, NC_PASSWORD_PLACEHOLDER) != 0) {
            strncpy(nc->password, pw, NETCONFIG_PASSWORD_LEN - 1);
            nc->password[NETCONFIG_PASSWORD_LEN - 1] = '\0';
        }
    }

    buf_copy(buf_nc_ip,       nc->ip_address, NETCONFIG_IPV4_LEN);
    buf_copy(buf_nc_netmask,  nc->netmask,    NETCONFIG_IPV4_LEN);
    buf_copy(buf_nc_gateway,  nc->gateway,    NETCONFIG_IPV4_LEN);
    buf_copy(buf_nc_dns,      nc->dns_server, NETCONFIG_IPV4_LEN);

    nc->country[0] = code[0];
    nc->country[1] = code[1];
    nc->country[2] = '\0';

    /* Canonical code only when the user actually cycled Change this
     * session; otherwise keep whatever raw value was loaded, even if it
     * is a non-canonical member of the same group (e.g. 4). */
    nc->auth_mode = nc_auth_touched ? nc_auth_codes[nc_auth_index] : nc_auth_original;

    nc->ip_mode = (nc_dlg[NC_STATIC_BTN].ob_state & SELECTED)
                      ? NETCONFIG_MODE_STATIC : NETCONFIG_MODE_DHCP;
    return 1;
}

/* Redraws just the NC_AUTH_VAL field without the full-dialog flicker of
 * objc_draw(nc_dlg, NC_ROOT, ...): fills exactly that object's rectangle
 * with white (VDI color 0 -- matches NC_ROOT's own obspec interiorcol,
 * decoded from 0x00031070) before drawing the new text over it, since
 * G_STRING draws never clear their own background (see the report). */
static void nc_redraw_auth_val(short dlg_x, short dlg_y, short dlg_w, short dlg_h)
{
    short cw, ch, bw, bh;
    short vdi;
    short ox, oy;
    short pxy[4];

    vdi = graf_handle(&cw, &ch, &bw, &bh);
    objc_offset(nc_dlg, NC_AUTH_VAL, &ox, &oy);

    pxy[0] = ox;
    pxy[1] = oy;
    pxy[2] = (short)(ox + nc_dlg[NC_AUTH_VAL].ob_width  - 1);
    pxy[3] = (short)(oy + nc_dlg[NC_AUTH_VAL].ob_height - 1);

    /* Without an explicit MD_REPLACE, v_bar() inherits whatever VDI
     * writing mode AES last left set (e.g. transparent/XOR for its own
     * text or highlight drawing), which is what produced the reversed
     * (black background, white text) fill instead of a solid white one. */
    vswr_mode(vdi, MD_REPLACE);
    vsf_interior(vdi, FIS_SOLID);
    vsf_color(vdi, WHITE);
    v_bar(vdi, pxy);

    objc_draw(nc_dlg, NC_AUTH_VAL, MAX_DEPTH, dlg_x, dlg_y, dlg_w, dlg_h);

    (void)cw; (void)ch; (void)bw; (void)bh;
}

/* Modal. OK commits the edit buffers into *nc (see nc_save_to_config());
 * Cancel discards them, leaving *nc exactly as it was on entry. ESC is
 * meant to behave like Cancel, per the same TOUCHEXIT/form_do() idiom
 * used throughout this file -- see the report for a caveat on this. */
static void netconfig_editor_run(NetConfig *nc)
{
    short x, y, w, h;
    short which;
    short start_obj;
    int done;

    nc_dialog_init();
    nc_load_from_config(nc);

    form_center(nc_dlg, &x, &y, &w, &h);
    form_dial(FMD_START, x, y, w, h, x, y, w, h);
    objc_draw(nc_dlg, NC_ROOT, MAX_DEPTH, x, y, w, h);

    done = 0;
    start_obj = NC_SSID_EDIT;
    while (!done) {
        which = (short)(form_do(nc_dlg, start_obj) & 0x7FFF);
        wait_mouse_release();
        start_obj = NC_SSID_EDIT;

        switch (which) {
        case NC_DHCP_BTN:
            nc_dlg[NC_DHCP_BTN].ob_state   |= (unsigned short)SELECTED;
            nc_dlg[NC_STATIC_BTN].ob_state &= (unsigned short)(~SELECTED);
            nc_apply_ip_field_state(NETCONFIG_MODE_DHCP);
            objc_draw(nc_dlg, NC_ROOT, MAX_DEPTH, x, y, w, h);
            break;

        case NC_STATIC_BTN:
            nc_dlg[NC_STATIC_BTN].ob_state |= (unsigned short)SELECTED;
            nc_dlg[NC_DHCP_BTN].ob_state   &= (unsigned short)(~SELECTED);
            nc_apply_ip_field_state(NETCONFIG_MODE_STATIC);
            objc_draw(nc_dlg, NC_ROOT, MAX_DEPTH, x, y, w, h);
            break;

        case NC_AUTH_BTN:
            nc_auth_index = (nc_auth_index + 1) % NC_AUTH_OPTION_COUNT;
            nc_auth_touched = 1;
            /* Space-padded to the full field width -- see the matching
             * comment in nc_load_from_config(). */
            sprintf(buf_nc_auth_val, "%-11.11s", nc_auth_names[nc_auth_index]);
            /* Targeted redraw (see nc_redraw_auth_val()) instead of the
             * full NC_ROOT redraw: avoids flickering the whole dialog for
             * a one-field text change. */
            nc_redraw_auth_val(x, y, w, h);
            break;

        case NC_OK:
            if (!nc_save_to_config(nc)) {
                start_obj = NC_COUNTRY_EDIT; /* refocus on the field that failed */
                break;
            }
            done = 1;
            break;

        case NC_CANCEL:
        default:
            done = 1;
            break;
        }
    }

    form_dial(FMD_FINISH, x, y, w, h, x, y, w, h);
}

/* ================================================================== */
/* Drive overview / main dialog                                        */
/* ================================================================== */

static void ov_dialog_init(void)
{
    short cw, ch, bw, bh;
    short sx, sy, sw, sh;
    int rh, tm, row_pitch;
    int DW, DH;
    int yt, ydiv1, yrow0, ydiv2, ybtn;
    int i;

    graf_handle(&cw, &ch, &bw, &bh);
    wind_get(0, WF_WORKXYWH, &sx, &sy, &sw, &sh);
    if (sh <= 0 || sh > 800) sh = 200;

    rh        = (sh >= 350) ? (int)ch : ((ch > 8) ? ch / 2 : (int)ch);
    tm        = (rh / 4 > 2) ? rh / 4 : 2;
    row_pitch = rh + 2;

    DW    = 46 * cw;
    yt    = tm;
    ydiv1 = yt + rh + 1;
    yrow0 = ydiv1 + 5;
    ydiv2 = yrow0 + MAX_DRIVES * row_pitch + 2;
    ybtn  = ydiv2 + 7;
    DH    = ybtn + rh + tm + 3;

    set_obj(ov_dlg, OV_ROOT, G_BOX, NONE, NORMAL, 0, 0, DW, DH);
    ov_dlg[OV_ROOT].ob_spec.index = 0x00031070L;

    set_obj(ov_dlg, OV_TITLE, G_STRING, NONE, NORMAL, 13*cw, yt, 20*cw, rh);
    ov_dlg[OV_TITLE].ob_spec.free_string = "SideTNFS Drives";

    set_obj(ov_dlg, OV_DIV1, G_BOX, NONE, NORMAL, cw, ydiv1, DW - 2*cw, 2);
    ov_dlg[OV_DIV1].ob_spec.index = 0x00001171L;

    for (i = 0; i < MAX_DRIVES; i++) {
        int ry = yrow0 + i * row_pitch;

        set_obj(ov_dlg, OV_ROW_TEXT(i), G_STRING, NONE, NORMAL, cw, ry, 32*cw, rh);
        ov_dlg[OV_ROW_TEXT(i)].ob_spec.free_string = ov_row_text[i];

        set_obj(ov_dlg, OV_ROW_EDIT(i), G_BUTTON, EXIT | TOUCHEXIT, NORMAL, 34*cw, ry, 8*cw, rh);
        ov_dlg[OV_ROW_EDIT(i)].ob_spec.free_string = " Edit ";
    }

    set_obj(ov_dlg, OV_DIV2, G_BOX, NONE, NORMAL, cw, ydiv2, DW - 2*cw, 2);
    ov_dlg[OV_DIV2].ob_spec.index = 0x00001171L;

    set_obj(ov_dlg, OV_ADD, G_BUTTON, EXIT | TOUCHEXIT, NORMAL, 4*cw, ybtn, 10*cw, rh);
    ov_dlg[OV_ADD].ob_spec.free_string = "Add disk";

    set_obj(ov_dlg, OV_OK, G_BUTTON, EXIT | DEFAULT | TOUCHEXIT, NORMAL, 18*cw, ybtn, 8*cw, rh);
    ov_dlg[OV_OK].ob_spec.free_string = "   OK   ";

    set_obj(ov_dlg, OV_CANCEL, G_BUTTON, EXIT | TOUCHEXIT, NORMAL, 30*cw, ybtn, 8*cw, rh);
    ov_dlg[OV_CANCEL].ob_spec.free_string = " Cancel ";

    wire_tree(ov_dlg, OV_NOBJS);

    (void)sx; (void)sy; (void)sw; (void)bw; (void)bh;
}

static void ov_refresh_rows(const DriveConfig *cfg)
{
    int i;
    for (i = 0; i < MAX_DRIVES; i++) {
        if (i < cfg->drive_count) {
            sprintf(ov_row_text[i], "%c:  %-20.20s%s",
                    cfg->drives[i].letter, cfg->drives[i].nickname,
                    drive_type_name(cfg->drives[i].type));
            ov_dlg[OV_ROW_EDIT(i)].ob_state &= (unsigned short)(~DISABLED);
        } else {
            ov_row_text[i][0] = '\0';
            ov_dlg[OV_ROW_EDIT(i)].ob_state |= (unsigned short)DISABLED;
        }
    }
}

/* Software cold-reset of the Atari itself (not the Pico). Clears the
 * three "memory valid" checksum longs at $420/$43A/$51A so TOS treats
 * RAM as invalid and performs a full reinit, then jumps through the
 * reset vector at $4 -- same effect as the proven inline-asm sequence
 * in sidecart-configurator-atari's main.c, expressed in plain C instead
 * of assembly. All of $420/$43A/$51A/$4 are below the low-memory
 * boundary TOS protects from user-mode access, so this must run in
 * supervisor mode via Supexec() -- never called directly. Never
 * returns. */
static long atari_do_reset(void)
{
    *(volatile long *)0x420L = 0;
    *(volatile long *)0x43AL = 0;
    *(volatile long *)0x51AL = 0;
    ((void (*)(void))(*(volatile long *)0x4L))();
    return 0; /* unreached */
}

/* The only place that ever writes to firmware: the status window's own
 * SAVE button (SW_SAVE). Covers both the drive list and, if the user
 * actually changed it, the wifi/network config (separate SET/SAVE
 * commands each, see save_to_firmware()/save_network_to_firmware()).
 * Stops at the first failure; a failed network save leaves g_netconfig
 * exactly as the user left it (never silently replaced), and drives
 * already saved before it stay saved regardless. Returns 1 if the
 * caller's window should close afterwards (drives and, if applicable,
 * network both fully saved and read back -- a reboot is needed before
 * either takes effect, so there is nothing left to do in this run), 0
 * to keep it open. */
static int perform_save(DriveConfig *cfg, int firmware_backed)
{
    int i;
    int all_valid = 1;
    char msg[220];
    int network_changed;

    if (drive_config_ordinary_count(cfg) > MAX_ORDINARY_DRIVES) {
        sprintf(msg, "[3][Validation error|Too many drives (max %d).][OK]", MAX_ORDINARY_DRIVES);
        form_alert(1, msg);
        all_valid = 0;
    }
    if (all_valid) {
        for (i = 0; i < cfg->drive_count; i++) {
            if (!validate_drive(&cfg->drives[i], cfg, i, msg)) {
                form_alert(1, msg);
                all_valid = 0;
                break;
            }
        }
    }

    /* Only validate/save network config if the user actually changed
     * it since it was loaded (or last saved) -- opening/closing the
     * Config dialog without real edits must never trigger a network
     * save. */
    network_changed = netconfig_changed(&g_netconfig, &g_netconfig_baseline);
    if (all_valid && network_changed) {
        if (!validate_netconfig(&g_netconfig, msg)) {
            form_alert(1, msg);
            all_valid = 0;
        }
    }

    if (!all_valid)
        return 0;

    if (!firmware_backed) {
        /* No firmware detected -- there is nowhere left to save to (no
         * local file fallback anymore). */
        form_alert(1, "[3][Save failed|No SideTNFS firmware detected.|Nothing was saved.][OK]");
        return 0;
    }

    /* Drives first (existing, proven flow); network second, only if
     * actually changed. Stop at the first failure either way. */
    if (!save_to_firmware(cfg, msg)) {
        form_alert(1, msg);
        return 0;
    }

    if (network_changed) {
        if (!save_network_to_firmware(&g_netconfig, msg)) {
            form_alert(1, msg);
            return 0; /* drives already saved; g_netconfig left untouched */
        }
        g_netconfig_baseline = g_netconfig; /* new known-good baseline */

        /* No Reset Now button here: an Atari reset does not reset the
         * Pico, so it would not actually make the new wifi settings
         * active -- offering it would contradict the message below.
         * Activating them needs a genuine Sidecartridge restart, which
         * this program has no way to trigger. */
        form_alert(1, "[1][Network settings saved.|They become active after|"
                      "restarting the Sidecartridge.][OK]");
    } else {
        /* Cancel is the default (button 1): a full-machine reset is
         * easy to trigger by accident otherwise. */
        if (form_alert(1, "[1][Configuration saved to SideTNFS.|Reboot required.|"
                          "Multi-drive mounting is not active yet.][Cancel|Reset Now]") == 2) {
            Supexec(atari_do_reset);
        }
    }
    return 1;
}

/* ================================================================== */
/* Drives window (Fase AC-6)                                           */
/* Add disk/edit/delete only -- no Config (that lives on the status     */
/* window now) and no firmware write here: OK and Cancel both just      */
/* close this window, keeping (OK) or leaving as-is (Cancel, which      */
/* never rolled back edits to begin with) whatever *cfg holds in        */
/* memory. The only place that ever writes to firmware is the status    */
/* window's own Save button (perform_save(), via SW_SAVE). */
/* ================================================================== */
static void drives_window_run(DriveConfig *cfg)
{
    short x, y, w, h;
    short which;
    int done;
    int i;

    ov_dialog_init();
    ov_refresh_rows(cfg);

    form_center(ov_dlg, &x, &y, &w, &h);
    form_dial(FMD_START, x, y, w, h, x, y, w, h);
    objc_draw(ov_dlg, OV_ROOT, MAX_DEPTH, x, y, w, h);

    done = 0;
    while (!done) {
        which = (short)(form_do(ov_dlg, OV_ROOT) & 0x7FFF);
        wait_mouse_release();

        if (which >= OV_ROW_BASE && which < OV_AFTER_ROWS) {
            int obj_offset  = which - OV_ROW_BASE;
            int row         = obj_offset / 2;
            int is_edit_btn = (obj_offset % 2) == 1;
            int result      = 0;
            Drive *d;

            if (is_edit_btn && row < cfg->drive_count) {
                d = &cfg->drives[row];
                switch (d->type) {
                case DRIVE_TYPE_CONFIG:
                    cl_editor_run(cfg, row);
                    break;
                case DRIVE_TYPE_TNFS:
                    result = tnfs_editor_run(cfg, row);
                    break;
                case DRIVE_TYPE_SD:
                    result = sd_editor_run(cfg, row);
                    break;
                }
                if (result == 2) {
                    for (i = row; i < cfg->drive_count - 1; i++)
                        cfg->drives[i] = cfg->drives[i + 1];
                    cfg->drive_count--;
                }
                /* Any edit/delete can change a letter or remove a row,
                 * so re-sort before redrawing -- the config drive's row
                 * position is not fixed, only its letter constraints. */
                drive_config_sort_by_letter(cfg);
                ov_refresh_rows(cfg);
                objc_draw(ov_dlg, OV_ROOT, MAX_DEPTH, x, y, w, h);
            }

        } else if (which == OV_ADD) {
            if (drive_config_ordinary_count(cfg) >= MAX_ORDINARY_DRIVES) {
                form_alert(1, "[3][Add disk|Maximum of 8 drives reached.][OK]");
            } else {
                int type = add_disk_type_run();
                if (type == DRIVE_TYPE_TNFS)
                    tnfs_editor_run(cfg, -1);
                else if (type == DRIVE_TYPE_SD)
                    sd_editor_run(cfg, -1);
                drive_config_sort_by_letter(cfg);
                ov_refresh_rows(cfg);
                objc_draw(ov_dlg, OV_ROOT, MAX_DEPTH, x, y, w, h);
            }

        } else if (which == OV_OK) {
            done = 1;

        } else if (which == OV_CANCEL) {
            done = 1;
        }
    }

    form_dial(FMD_FINISH, x, y, w, h, x, y, w, h);
}

/* ================================================================== */
/* Status window (Fase AC-6) -- the new top-level main window           */
/* ================================================================== */

static void sw_dialog_init(void)
{
    short cw, ch, bw, bh;
    short sx, sy, ssw, sh;
    int rh, tm, pitch;
    int DW, DH, xl;
    int yt, ydiv1, ylblnet, ynet0, ynet1, ynet2, ynet3, ydiv2,
        ylblclock, yclock0, yclock1, ydiv3, ylbldrv, ydrv0, ydiv4, ybtn;
    int i;

    graf_handle(&cw, &ch, &bw, &bh);
    wind_get(0, WF_WORKXYWH, &sx, &sy, &ssw, &sh);
    if (sh <= 0 || sh > 800) sh = 200;

    rh    = (sh >= 350) ? (int)ch : ((ch > 8) ? ch / 2 : (int)ch);
    tm    = (rh / 4 > 2) ? rh / 4 : 2;
    pitch = rh + 2; /* tighter than the editors' rh+3: read-only status text */

    DW = 44 * cw;
    xl = 2 * cw;

    yt        = tm;
    ydiv1     = yt + rh + 1;
    ylblnet   = ydiv1 + 5;
    ynet0     = ylblnet + pitch;
    ynet1     = ynet0 + pitch;
    ynet2     = ynet1 + pitch;
    ynet3     = ynet2 + pitch;
    ydiv2     = ynet3 + rh + 2;
    ylblclock = ydiv2 + 2;
    yclock0   = ylblclock + pitch;
    yclock1   = yclock0 + pitch;
    ydiv3     = yclock1 + rh + 2;
    ylbldrv   = ydiv3 + 2;
    ydrv0     = ylbldrv + pitch;
    ydiv4     = ydrv0 + SW_NUM_DRIVE_LINES * pitch + 2;
    ybtn      = ydiv4 + 7;
    DH        = ybtn + rh + tm + 3;

    set_obj(sw_dlg, SW_ROOT, G_BOX, NONE, NORMAL, 0, 0, DW, DH);
    sw_dlg[SW_ROOT].ob_spec.index = 0x00031070L;

    set_obj(sw_dlg, SW_TITLE, G_STRING, NONE, NORMAL, 10*cw, yt, 24*cw, rh);
    sw_dlg[SW_TITLE].ob_spec.free_string = "SIDETNFS Configuration";

    set_obj(sw_dlg, SW_DIV1, G_BOX, NONE, NORMAL, cw, ydiv1, DW - 2*cw, 2);
    sw_dlg[SW_DIV1].ob_spec.index = 0x00001171L;

    set_obj(sw_dlg, SW_LBL_NETWORK, G_STRING, NONE, NORMAL, xl, ylblnet, 12*cw, rh);
    sw_dlg[SW_LBL_NETWORK].ob_spec.free_string = "Network";

    set_obj(sw_dlg, SW_NET_LINE_0, G_STRING, NONE, NORMAL, xl, ynet0, 40*cw, rh);
    sw_dlg[SW_NET_LINE_0].ob_spec.free_string = sw_net_line[0];
    set_obj(sw_dlg, SW_NET_LINE_1, G_STRING, NONE, NORMAL, xl, ynet1, 40*cw, rh);
    sw_dlg[SW_NET_LINE_1].ob_spec.free_string = sw_net_line[1];
    set_obj(sw_dlg, SW_NET_LINE_2, G_STRING, NONE, NORMAL, xl, ynet2, 40*cw, rh);
    sw_dlg[SW_NET_LINE_2].ob_spec.free_string = sw_net_line[2];
    set_obj(sw_dlg, SW_NET_LINE_3, G_STRING, NONE, NORMAL, xl, ynet3, 40*cw, rh);
    sw_dlg[SW_NET_LINE_3].ob_spec.free_string = sw_net_line[3];

    set_obj(sw_dlg, SW_DIV2, G_BOX, NONE, NORMAL, cw, ydiv2, DW - 2*cw, 2);
    sw_dlg[SW_DIV2].ob_spec.index = 0x00001171L;

    set_obj(sw_dlg, SW_LBL_CLOCK, G_STRING, NONE, NORMAL, xl, ylblclock, 12*cw, rh);
    sw_dlg[SW_LBL_CLOCK].ob_spec.free_string = "Clock";

    set_obj(sw_dlg, SW_CLOCK_LINE_0, G_STRING, NONE, NORMAL, xl, yclock0, 40*cw, rh);
    sw_dlg[SW_CLOCK_LINE_0].ob_spec.free_string = sw_clock_line[0];
    set_obj(sw_dlg, SW_CLOCK_LINE_1, G_STRING, NONE, NORMAL, xl, yclock1, 40*cw, rh);
    sw_dlg[SW_CLOCK_LINE_1].ob_spec.free_string = sw_clock_line[1];

    set_obj(sw_dlg, SW_DIV3, G_BOX, NONE, NORMAL, cw, ydiv3, DW - 2*cw, 2);
    sw_dlg[SW_DIV3].ob_spec.index = 0x00001171L;

    set_obj(sw_dlg, SW_LBL_DRIVES, G_STRING, NONE, NORMAL, xl, ylbldrv, 16*cw, rh);
    sw_dlg[SW_LBL_DRIVES].ob_spec.free_string = "Active drives";

    for (i = 0; i < SW_NUM_DRIVE_LINES; i++) {
        int ry = ydrv0 + i * pitch;
        set_obj(sw_dlg, SW_DRIVE_LINE(i), G_STRING, NONE, NORMAL, xl, ry, 40*cw, rh);
        sw_dlg[SW_DRIVE_LINE(i)].ob_spec.free_string = sw_drive_line[i];
    }

    set_obj(sw_dlg, SW_DIV4, G_BOX, NONE, NORMAL, cw, ydiv4, DW - 2*cw, 2);
    sw_dlg[SW_DIV4].ob_spec.index = 0x00001171L;

    set_obj(sw_dlg, SW_CONFIG, G_BUTTON, EXIT | TOUCHEXIT, NORMAL, 2*cw, ybtn, 9*cw, rh);
    sw_dlg[SW_CONFIG].ob_spec.free_string = " CONFIG  ";

    set_obj(sw_dlg, SW_DRIVES, G_BUTTON, EXIT | TOUCHEXIT, NORMAL, 12*cw, ybtn, 9*cw, rh);
    sw_dlg[SW_DRIVES].ob_spec.free_string = " DRIVES  ";

    set_obj(sw_dlg, SW_SAVE, G_BUTTON, EXIT | DEFAULT | TOUCHEXIT, NORMAL, 22*cw, ybtn, 9*cw, rh);
    sw_dlg[SW_SAVE].ob_spec.free_string = "  SAVE   ";

    set_obj(sw_dlg, SW_QUIT, G_BUTTON, EXIT | TOUCHEXIT, NORMAL, 32*cw, ybtn, 9*cw, rh);
    sw_dlg[SW_QUIT].ob_spec.free_string = "  QUIT   ";

    wire_tree(sw_dlg, SW_NOBJS);

    (void)sx; (void)sy; (void)ssw; (void)bw; (void)bh;
}

/* Reads only g_netconfig (the one-time network_startup_load() result,
 * kept current afterwards by the CONFIG editor) and *cfg (the local
 * DriveConfig, for the TNFS server line) -- never calls
 * sidetnfs_probe_get_network_config() itself. GET_NETWORK_CONFIG/0x0413
 * is sent exactly once per session, in network_startup_load(). */
static void status_refresh_network(const DriveConfig *cfg)
{
    int i;
    int tnfs_count = 0;
    const Drive *first_tnfs = 0;

    /* "Configured" only means the saved configuration was read
     * successfully -- 0x0413 returns stored config, not a live Wi-Fi
     * association proof, so this is deliberately never "Connected". */
    switch (g_netconfig_load_state) {
    case NETLOAD_OK:
        strncpy(sw_net_line[0], "Status: Configured", SW_LINE_BUF - 1);
        break;
    case NETLOAD_BAD_STATUS:
        sprintf(sw_net_line[0], "Status: %s", netconfig_status_text(g_netconfig_load_fw_status));
        break;
    case NETLOAD_UNAVAILABLE:
    default:
        strncpy(sw_net_line[0], "Status: Unavailable", SW_LINE_BUF - 1);
        break;
    }
    sw_net_line[0][SW_LINE_BUF - 1] = '\0';

    /* SSID only -- the password is never shown here. */
    if (g_netconfig.ssid[0] != '\0')
        sprintf(sw_net_line[1], "SSID: %s", g_netconfig.ssid);
    else
        strncpy(sw_net_line[1], "SSID: -", SW_LINE_BUF - 1);
    sw_net_line[1][SW_LINE_BUF - 1] = '\0';

    /* DHCP mode shows "DHCP", not the stored static-IP field -- the
     * actual DHCP-assigned address is unknown to this protocol. */
    if (g_netconfig.ip_mode == NETCONFIG_MODE_DHCP)
        strncpy(sw_net_line[2], "IP address: DHCP", SW_LINE_BUF - 1);
    else if (g_netconfig.ip_address[0] != '\0')
        sprintf(sw_net_line[2], "IP address: %s", g_netconfig.ip_address);
    else
        strncpy(sw_net_line[2], "IP address: -", SW_LINE_BUF - 1);
    sw_net_line[2][SW_LINE_BUF - 1] = '\0';

    /* TNFS server comes from the local DriveConfig, not g_netconfig --
     * no firmware command sent for this line either. */
    for (i = 0; i < cfg->drive_count; i++) {
        if (cfg->drives[i].type == DRIVE_TYPE_TNFS) {
            if (tnfs_count == 0)
                first_tnfs = &cfg->drives[i];
            tnfs_count++;
        }
    }
    if (tnfs_count == 0)
        strncpy(sw_net_line[3], "TNFS server: -", SW_LINE_BUF - 1);
    else if (tnfs_count == 1)
        sprintf(sw_net_line[3], "TNFS server: %.28s:%d", first_tnfs->host, first_tnfs->port);
    else
        sprintf(sw_net_line[3], "TNFS server: %.20s +%d", first_tnfs->host, tnfs_count - 1);
    sw_net_line[3][SW_LINE_BUF - 1] = '\0';
}

/* Fase AC-6: placeholder -- RTC settings/status follow in a separate
 * phase, per "Not doen". */
static void status_refresh_clock(void)
{
    strncpy(sw_clock_line[0], "NTP: Not synchronized", SW_LINE_BUF - 1); sw_clock_line[0][SW_LINE_BUF - 1] = '\0';
    strncpy(sw_clock_line[1], "Timezone: -",           SW_LINE_BUF - 1); sw_clock_line[1][SW_LINE_BUF - 1] = '\0';
}

/* Real data from the existing DriveConfig model. Caps the visible list
 * at SW_NUM_DRIVE_LINES (this is a compact summary, not the full editor
 * -- DRIVES still shows every configured drive) and shows an overflow
 * note on the last line rather than silently dropping entries. */
static void status_refresh_drives(const DriveConfig *cfg)
{
    int i;
    int shown;
    int overflow;

    if (cfg->drive_count == 0) {
        strncpy(sw_drive_line[0], "No active drives", SW_LINE_BUF - 1);
        sw_drive_line[0][SW_LINE_BUF - 1] = '\0';
        for (i = 1; i < SW_NUM_DRIVE_LINES; i++)
            sw_drive_line[i][0] = '\0';
        return;
    }

    overflow = cfg->drive_count > SW_NUM_DRIVE_LINES;
    shown = overflow ? (SW_NUM_DRIVE_LINES - 1) : cfg->drive_count;

    for (i = 0; i < shown; i++) {
        const Drive *d = &cfg->drives[i];
        const char *path;

        switch (d->type) {
        case DRIVE_TYPE_TNFS: path = (d->mount_path[0] != '\0') ? d->mount_path : "/"; break;
        case DRIVE_TYPE_SD:   path = d->sd_path; break;
        case DRIVE_TYPE_CONFIG:
        default:              path = "-"; break;
        }
        sprintf(sw_drive_line[i], "%c:  %-10.10s %-6s %-18.18s",
                d->letter, d->nickname, drive_type_name(d->type), path);
    }

    if (overflow) {
        sprintf(sw_drive_line[shown], "...and %d more (see Drives)", cfg->drive_count - shown);
        shown++;
    }

    for (i = shown; i < SW_NUM_DRIVE_LINES; i++)
        sw_drive_line[i][0] = '\0';
}

/* Updates all three status sections' text buffers. Does not redraw --
 * matches ov_refresh_rows()'s existing convention of leaving objc_draw()
 * to the caller, since callers often need to draw other objects too. */
static void status_window_refresh(const DriveConfig *cfg)
{
    status_refresh_network(cfg);
    status_refresh_clock();
    status_refresh_drives(cfg);
}

/* ================================================================== */
/* Main entry point                                                    */
/* ================================================================== */

void dialog_run(DriveConfig *cfg)
{
    short x, y, w, h;
    short which;
    int done;
    int firmware_backed;

    shared_fields_init();

    /* No local file storage: *cfg starts out as the built-in defaults
     * (drive_config_init_defaults(), set by main.c) and is fully
     * replaced by the firmware's own drive list when one is found. See
     * dialog_startup_load(). Without firmware, Save has nothing to
     * write to and says so. */
    firmware_backed = dialog_startup_load(cfg);

    /* Fase AC-4 (network protocol): loaded after the drive protocol,
     * regardless of firmware_backed -- with no firmware at all this
     * simply times out too and falls back the same way. */
    network_startup_load();

    sw_dialog_init();
    status_window_refresh(cfg);

    form_center(sw_dlg, &x, &y, &w, &h);
    form_dial(FMD_START, x, y, w, h, x, y, w, h);
    objc_draw(sw_dlg, SW_ROOT, MAX_DEPTH, x, y, w, h);

    done = 0;
    while (!done) {
        which = (short)(form_do(sw_dlg, SW_ROOT) & 0x7FFF);
        wait_mouse_release();

        switch (which) {
        case SW_CONFIG:
            netconfig_editor_run(&g_netconfig);
            /* Reflect the user's OK'd edits (or, on Cancel, the unchanged
             * g_netconfig -- netconfig_editor_run() already guarantees
             * that) immediately. No new probe: g_netconfig is already
             * current, this only rebuilds the status window's own text. */
            status_refresh_network(cfg);
            objc_draw(sw_dlg, SW_ROOT, MAX_DEPTH, x, y, w, h);
            break;

        case SW_DRIVES:
            drives_window_run(cfg);
            status_window_refresh(cfg);
            objc_draw(sw_dlg, SW_ROOT, MAX_DEPTH, x, y, w, h);
            break;

        case SW_SAVE:
            if (perform_save(cfg, firmware_backed))
                done = 1;
            objc_draw(sw_dlg, SW_ROOT, MAX_DEPTH, x, y, w, h);
            break;

        case SW_QUIT:
        default:
            done = 1;
            break;
        }
    }

    form_dial(FMD_FINISH, x, y, w, h, x, y, w, h);
}
