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
#include "rtcconfig.h"
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
    SD_LBL_ACTIVE, SD_ACTIVE_BTN,
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
    TE_LBL_ACTIVE, TE_ACTIVE_BTN,
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
/* Clock/NTP editor (RC_*)                                             */
/* Fase 12, v1 -- KISS: opened via the CLOCK button at the bottom of the */
/* WiFi/Network settings editor (NC_*), not from the main status window */
/* directly (that dialog is already full -- see the report). UI only,  */
/* same "temporary working buffers, committed only on OK" idiom as      */
/* every other editor in this file; no firmware read/write yet.         */
/* ================================================================== */
enum {
    RC_ROOT = 0,
    RC_TITLE,
    RC_DIV1,
    RC_LBL_NTP,    RC_NTP_BTN,
    RC_LBL_SERVER, RC_SERVER_EDIT,
    RC_LBL_OFFSET, RC_OFFSET_EDIT,
    RC_DIV2,
    RC_OK, RC_CANCEL,
    RC_NOBJS
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
    NC_CLOCK_BTN, NC_OK, NC_CANCEL,
    NC_NOBJS
};

/* ================================================================== */
/* Drive overview / main dialog (OV_*)                                 */
/* AtariConfig-3: eight FIXED rows (index == firmware slot index),      */
/* never compacted/re-sorted, plus one row for the SETTINGS disk        */
/* letter (not one of the eight -- see drive.h). Each slot row's own    */
/* button reads "Add" (EMPTY) or "Edit" (DISABLED/ENABLED); there is no */
/* separate top-level Add button anymore, since there is no longer a    */
/* single well-defined "next free slot" -- the user picks the slot by   */
/* clicking its own row.                                                */
/* ================================================================== */
enum {
    OV_ROOT = 0,
    OV_TITLE,
    OV_DIV1,
    OV_LBL_SETTINGS, OV_SETTINGS_VAL, OV_SETTINGS_BTN,
    OV_DIV2,
    OV_ROW_BASE
};
#define OV_ROW_TEXT(i) (OV_ROW_BASE + (i)*2 + 0)
#define OV_ROW_BTN(i)  (OV_ROW_BASE + (i)*2 + 1)
#define OV_AFTER_ROWS  (OV_ROW_BASE + MAX_DRIVES*2)
#define OV_DIV3   (OV_AFTER_ROWS + 0)
#define OV_OK     (OV_AFTER_ROWS + 1)
#define OV_CANCEL (OV_AFTER_ROWS + 2)
#define OV_NOBJS  (OV_AFTER_ROWS + 3)

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
    /* Network: no separate heading any more -- "Network:" is itself the
     * first value line, carrying the live connection status. */
    SW_NET_LINE_0, SW_NET_LINE_1, SW_NET_LINE_2,
    SW_DIV2,
    /* Clock: the "Clock:" label and the live date/time are separate
     * objects so a tick can repaint the time alone. */
    SW_CLOCK_LBL,
    SW_CLOCK_NOW,
    SW_CLOCK_NTP,
    SW_DIV3,
    SW_DRIVE_HEADER,                 /* "Active drives:" + the count, right-aligned */
    SW_DRIVE_LINE_BASE
};
#define SW_NUM_DRIVE_LINES   5
#define SW_DRIVE_LINE(i)     (SW_DRIVE_LINE_BASE + (i))
#define SW_AFTER_DRIVE_LINES (SW_DRIVE_LINE_BASE + SW_NUM_DRIVE_LINES)
#define SW_DRIVE_MORE (SW_AFTER_DRIVE_LINES + 0) /* "(and N more)", blank when everything fits */
#define SW_DIV4    (SW_AFTER_DRIVE_LINES + 1)
#define SW_CONFIG  (SW_AFTER_DRIVE_LINES + 2)
#define SW_DRIVES  (SW_AFTER_DRIVE_LINES + 3)
#define SW_SAVE    (SW_AFTER_DRIVE_LINES + 4)
#define SW_QUIT    (SW_AFTER_DRIVE_LINES + 5)
#define SW_NOBJS   (SW_AFTER_DRIVE_LINES + 6)

static OBJECT cl_dlg[CL_NOBJS];
static OBJECT sd_dlg[SD_NOBJS];
static OBJECT te_dlg[TE_NOBJS];
static OBJECT nc_dlg[NC_NOBJS];
static OBJECT rc_dlg[RC_NOBJS];
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

/* AtariConfig-3: Active/Inactive toggle, shared between the SD and TNFS
 * editors (never open simultaneously, same sharing convention as the
 * buffers above) -- single button, own text doubles as the value
 * display, same "button with changing text" idiom as the Clock/NTP
 * editor's NTP-enabled toggle. Only meaningful for a slot that is
 * DISABLED or ENABLED at OK time; an EMPTY slot defaults to ENABLED
 * unless the user explicitly toggles it off before OK (see the report). */
#define DRIVE_ACTIVE_BUF 11
static char buf_drive_active[DRIVE_ACTIVE_BUF];
static int drive_editor_enabled; /* 1 = Active/ENABLED, 0 = Inactive/DISABLED -- live edit state */

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

/* Snapshot of the last known-good (loaded-from-firmware or
 * successfully-saved) drive configuration -- same changed-since-baseline
 * idiom as g_netconfig_baseline/g_rtcconfig_baseline below, used only to
 * warn on Quit with unsaved drive changes (see dialog_run()). */
static DriveConfig g_driveconfig_baseline;

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
/* Clock/NTP editable-field buffers/TEDINFOs (RC_*, Fase 12 v1)        */
/* NTP server reuses tmpl_host/vld_host (both 64 bytes, same as         */
/* RTCCONFIG_NTP_SERVER_LEN) rather than allocating a duplicate         */
/* template -- same sharing convention sd_path already uses for the     */
/* drive editors. Free-form 'X' validation only, same "no character-    */
/* class masking, real checks happen at OK time" convention as every    */
/* other text field in this file (see shared_fields_init()). */
#define RC_BUF_SERVER RTCCONFIG_NTP_SERVER_LEN /* 64 */
#define RC_BUF_OFFSET RTCCONFIG_UTC_OFFSET_LEN /* 4: "-12"/"+14" + NUL */

static char buf_rc_server[RC_BUF_SERVER];
static char buf_rc_offset[RC_BUF_OFFSET];
static char tmpl_rc_offset[RC_BUF_OFFSET], vld_rc_offset[RC_BUF_OFFSET];
static TEDINFO ti_rc_server, ti_rc_offset;

/* RC_NTP_BTN's own text doubles as the Yes/No display -- no separate
 * value label, per the report's "button with changing text" fallback. */
#define RC_BUF_NTP_VAL 9
static char buf_rc_ntp_val[RC_BUF_NTP_VAL];
static int rc_ntp_enabled; /* live edit state while the dialog is open */

/* Session-persistent: survives across dialog opens/closes, like
 * g_netconfig. */
static RtcConfig g_rtcconfig;
/* Snapshot of the last known-good (loaded-from-firmware or
 * successfully-saved) RTC config -- same changed-since-baseline idiom
 * as g_netconfig_baseline, see perform_save(). */
static RtcConfig g_rtcconfig_baseline;

/* ================================================================== */
/* Drive-overview row text buffers                                     */
/* ================================================================== */
#define OV_ROW_BUF 44
static char ov_row_text[MAX_DRIVES][OV_ROW_BUF];
#define OV_SETTINGS_BUF 24
static char ov_settings_val[OV_SETTINGS_BUF];

/* ================================================================== */
/* Status window text buffers (Fase AC-6)                              */
/* ================================================================== */
#define SW_LINE_BUF 48

/* Every status line object is laid out SW_LINE_COLS characters wide at the
 * section's left margin, so a line formatted to exactly that many
 * characters ends flush with the box's right margin. That is what makes
 * "right-aligned" here a property of the text, not of a pixel position. */
#define SW_LINE_COLS 40

/* "Network: ", "SSID:    ", "IP:      ", "Clock:   ", "NTP:     " all put
 * their value in the same column. */
#define SW_LABEL_COLS 9

static char sw_net_line[3][SW_LINE_BUF];
static char sw_clock_ntp[SW_LINE_BUF];
static char sw_drive_header[SW_LINE_BUF];
static char sw_drive_line[SW_NUM_DRIVE_LINES][SW_LINE_BUF];
static char sw_drive_more[SW_LINE_BUF];

/* Live Atari clock shown on the Clock header line. "DD-MM-YYYY HH:MM:SS"
 * is 19 characters; the buffer leaves room for a NUL and then some. */
#define SW_CLOCK_TEXT_BUF   24
#define SW_CLOCK_FULL_CHARS 19
#define SW_CLOCK_SHORT_CHARS 8 /* "HH:MM:SS", used when the field is too narrow */
static char sw_clock_now[SW_CLOCK_TEXT_BUF];

/* How many characters the clock field is laid out for -- decided once in
 * sw_dialog_init() from the real window geometry, never a fixed guess. 0
 * means there was no room at all and the clock is simply not shown. */
static int sw_clock_field_chars;

/* One second. GEMDOS keeps seconds in two-second steps, so a faster tick
 * would only redraw the same text; anything slower would visibly lag. */
#define SW_CLOCK_TICK_MS 1000

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

static void update_drive_active_button_text(void)
{
    strncpy(buf_drive_active, drive_editor_enabled ? " Active   " : " Inactive ", DRIVE_ACTIVE_BUF - 1);
    buf_drive_active[DRIVE_ACTIVE_BUF - 1] = '\0';
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

    /* Fase 12 v1: same free-text ('X') convention -- the actual format
     * (whole hour, -12..+14, optional leading '+') is enforced at OK
     * time in rc_save_to_config(), not via TEDINFO character classes. */
    fill_n(tmpl_rc_offset, '_', RC_BUF_OFFSET - 1); fill_n(vld_rc_offset, 'X', RC_BUF_OFFSET - 1);

    init_ti(&ti_rc_server, buf_rc_server, tmpl_host,      vld_host,      RC_BUF_SERVER);
    init_ti(&ti_rc_offset, buf_rc_offset, tmpl_rc_offset, vld_rc_offset, RC_BUF_OFFSET);

    rtcconfig_init_defaults(&g_rtcconfig);
}

/* ================================================================== */
/* Shared drive validation -- used both by each editor's Test/OK and by */
/* Save's full-list check. msg must be at least 100 bytes.             */
/* ================================================================== */
static int validate_drive(const Drive *d, const DriveConfig *cfg, int skip_index, char *msg)
{
    int i;

    if (d->letter < 'A' || d->letter > 'Z' || d->letter == 'A' || d->letter == 'B') {
        sprintf(msg, "[3][Validation error|Drive %c: invalid letter.][OK]", d->letter);
        return 0;
    }
    if (d->letter == cfg->settings_letter) {
        sprintf(msg, "[3][Validation error|Drive letter %c: is reserved|for the SETTINGS disk.][OK]", d->letter);
        return 0;
    }
    for (i = 0; i < MAX_DRIVES; i++) {
        if (i == skip_index) continue;
        if (drive_slot_is_configured(&cfg->drives[i]) && cfg->drives[i].letter == d->letter) {
            sprintf(msg, "[3][Validation error|Drive letter %c: is already used|by slot %d.][OK]", d->letter, i + 1);
            return 0;
        }
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
    default:
        break;
    }
    return 1;
}

/* Symbolic text for every sidetnfs_config_status_t value (sidetnfs_config.h),
 * same convention as netconfig_status_text()/rtcconfig_status_text() --
 * every drive/SETTINGS-letter protocol error the user can hit (SET_DRIVE,
 * DELETE_DRIVE, SET_CONFIG_DRIVE, SAVE_CONFIG) is shown by name, never as
 * a bare status number. */
static const char *sidetnfs_drive_status_text(unsigned long status)
{
    switch (status) {
    case SIDETNFS_STATUS_OK:                     return "OK.";
    case SIDETNFS_STATUS_INVALID_INDEX:          return "Invalid slot index.";
    case SIDETNFS_STATUS_EMPTY_SLOT:             return "Slot is empty.";
    case SIDETNFS_STATUS_INVALID_DRIVE_LETTER:   return "Invalid drive letter.";
    case SIDETNFS_STATUS_DUPLICATE_DRIVE_LETTER: return "Duplicate drive letter.";
    case SIDETNFS_STATUS_INVALID_TYPE:           return "Invalid drive type.";
    case SIDETNFS_STATUS_INVALID_TRANSPORT:      return "Invalid transport.";
    case SIDETNFS_STATUS_INVALID_PORT:           return "Invalid port.";
    case SIDETNFS_STATUS_INVALID_HOST:           return "Invalid host.";
    case SIDETNFS_STATUS_INVALID_MOUNT_PATH:     return "Invalid mount path.";
    case SIDETNFS_STATUS_INVALID_SD_PATH:        return "Invalid SD path.";
    case SIDETNFS_STATUS_TOO_MANY_DRIVES:        return "Too many drives.";
    case SIDETNFS_STATUS_FLASH_WRITE_FAILED:     return "Flash write failed.";
    case SIDETNFS_STATUS_CRC_MISMATCH:           return "Flash CRC mismatch.";
    case SIDETNFS_STATUS_UNSUPPORTED_VERSION:    return "Unsupported protocol version.";
    case SIDETNFS_STATUS_INVALID_DRIVE_STATE:    return "Invalid drive state.";
    default:                                     return "Unknown status.";
    }
}

/* ================================================================== */
/* Firmware probe (Fase AC-4, protocol v3). Shows the GET_CONFIG_INFO   */
/* summary, then the record at a fixed representative slot 1 (the UI    */
/* does not yet track a per-drive firmware slot index for an            */
/* in-progress edit). Explicitly not a network connection test of the   */
/* drive being edited.                                                  */
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
        sprintf(msg, "[3][SideTNFS firmware|%s][OK]", sidetnfs_drive_status_text(config_info.status));
        form_alert(1, msg);
        return;
    }
    if (config_info.protocol_version != SIDETNFS_CONFIG_PROTOCOL_VERSION) {
        sprintf(msg, "[3][Unsupported SideTNFS configuration protocol|Firmware: %lu|Application: %lu][OK]",
                config_info.protocol_version, SIDETNFS_CONFIG_PROTOCOL_VERSION);
        form_alert(1, msg);
        return;
    }

    sprintf(msg, "[1][Firmware configuration|SETTINGS disk: %c:|Configured drives: %lu][OK]",
            (char)config_info.config_drive_letter, config_info.drive_count);
    form_alert(1, msg);

    if (sidetnfs_probe_get_drive(0, &drive_info) != SIDETNFS_PROBE_OK) {
        form_alert(1, "[3][SideTNFS drive|GET_DRIVE(0) timed out.][OK]");
        return;
    }
    if (drive_info.status != SIDETNFS_STATUS_OK) {
        sprintf(msg, "[3][SideTNFS drive|%s][OK]", sidetnfs_drive_status_text(drive_info.status));
        form_alert(1, msg);
        return;
    }

    switch (drive_info.state) {
    case SIDETNFS_DRIVE_STATE_EMPTY:
        form_alert(1, "[1][Slot 1|Empty.][OK]");
        break;
    case SIDETNFS_DRIVE_STATE_DISABLED:
    case SIDETNFS_DRIVE_STATE_ENABLED:
        if (drive_info.type == SIDETNFS_DRIVE_TYPE_TNFS) {
            sprintf(msg, "[1][Slot 1 (not a network test)|%c: %s|%s:%lu|%s / %s][OK]",
                    (char)drive_info.letter, drive_info.nickname,
                    drive_info.host, drive_info.port,
                    drive_info.transport == SIDETNFS_TRANSPORT_UDP ? "UDP" : "TCP",
                    drive_info.mount_path);
        } else if (drive_info.type == SIDETNFS_DRIVE_TYPE_SD) {
            sprintf(msg, "[1][Slot 1 (not a network test)|%c: %s|SD|%s][OK]",
                    (char)drive_info.letter, drive_info.nickname, drive_info.sd_path);
        } else {
            sprintf(msg, "[3][Slot 1|Unknown drive type %lu.][OK]", drive_info.type);
        }
        form_alert(1, msg);
        break;
    default:
        sprintf(msg, "[3][Slot 1|Unexpected drive state %lu.][OK]", drive_info.state);
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

/* Caller must have already validated w->type is SD or TNFS whenever
 * w->state != EMPTY -- an unrecognized type on a configured slot is a
 * load-time error, not something to silently default to TNFS here. */
static void wire_to_ui_drive(const SideTnfsDriveInfo *w, Drive *d)
{
    memset(d, 0, sizeof(*d));

    switch (w->state) {
    case SIDETNFS_DRIVE_STATE_DISABLED: d->state = DRIVE_SLOT_DISABLED; break;
    case SIDETNFS_DRIVE_STATE_ENABLED:  d->state = DRIVE_SLOT_ENABLED;  break;
    case SIDETNFS_DRIVE_STATE_EMPTY:
    default:                            d->state = DRIVE_SLOT_EMPTY;   break;
    }
    if (d->state == DRIVE_SLOT_EMPTY)
        return; /* every other field stays zeroed -- meaningless when EMPTY */

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

    switch (d->state) {
    case DRIVE_SLOT_DISABLED: w->state = SIDETNFS_DRIVE_STATE_DISABLED; break;
    case DRIVE_SLOT_ENABLED:  w->state = SIDETNFS_DRIVE_STATE_ENABLED;  break;
    case DRIVE_SLOT_EMPTY:
    default:                  w->state = SIDETNFS_DRIVE_STATE_EMPTY;   break;
    }
    if (d->state == DRIVE_SLOT_EMPTY)
        return; /* every other field stays zeroed (memset above) */

    w->letter = (unsigned long)(unsigned char)d->letter;
    w->type   = (d->type == DRIVE_TYPE_SD) ? SIDETNFS_DRIVE_TYPE_SD : SIDETNFS_DRIVE_TYPE_TNFS;
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
 * for all eight FIXED ordinary slots, always -- EMPTY is a normal state
 * (protocol v3), never a reason to skip a slot or stop early. Returns 1
 * on a fully consistent read of all eight slots, 0 on any timeout,
 * unexpected status/protocol version, or unrecognized field on a
 * configured slot -- *out is left untouched on failure (never partially
 * filled), so a communication problem partway through can never result
 * in a partially-loaded configuration being used or saved. No alerts:
 * this is the silent building block for both the startup load and the
 * post-Save readback verification, which report failure differently. */
static int fetch_drive_config_from_firmware(DriveConfig *out)
{
    SideTnfsConfigInfo config_info;
    SideTnfsDriveInfo wire;
    DriveConfig built;
    int i;
    char letter;

    if (sidetnfs_probe_get_config_info(&config_info) != SIDETNFS_PROBE_OK)
        return 0;
    if (config_info.status != SIDETNFS_STATUS_OK)
        return 0;
    if (config_info.protocol_version != SIDETNFS_CONFIG_PROTOCOL_VERSION)
        return 0;
    if (config_info.max_drives != (unsigned long)MAX_ORDINARY_DRIVES)
        return 0;

    letter = (char)config_info.config_drive_letter;
    if (letter < 'A' || letter > 'Z' || letter == 'A' || letter == 'B')
        return 0;

    memset(&built, 0, sizeof(built));
    built.settings_letter = letter;

    for (i = 0; i < MAX_ORDINARY_DRIVES; i++) {
        if (sidetnfs_probe_get_drive((unsigned long)i, &wire) != SIDETNFS_PROBE_OK)
            return 0;
        if (wire.status != SIDETNFS_STATUS_OK)
            return 0; /* only an out-of-range index is ever non-OK now -- never sent here */
        if (wire.state != SIDETNFS_DRIVE_STATE_EMPTY
            && wire.type != SIDETNFS_DRIVE_TYPE_SD && wire.type != SIDETNFS_DRIVE_TYPE_TNFS)
            return 0; /* unknown/corrupt type on a configured slot -- never silently treated as TNFS */

        wire_to_ui_drive(&wire, &built.drives[i]);
    }

    *out = built;
    return 1;
}

/* Startup wrapper: distinguishes "no firmware at all" (silent, expected
 * offline case -- a plain GET_CONFIG_INFO timeout), "firmware speaks an
 * unsupported protocol version" (a specific, visible mismatch -- see
 * the report for why this must never silently fall back to old-protocol
 * semantics), and "firmware present but its configuration is otherwise
 * unusable" (visible alert, since that is not the normal offline case).
 * On success, *cfg is entirely firmware-driven; on any failure *cfg
 * keeps whatever main.c initialized it to (built-in defaults) and Save
 * stays blocked (see dialog_run()'s firmware_backed). */
static int dialog_startup_load(DriveConfig *cfg)
{
    SideTnfsConfigInfo probe;
    DriveConfig fw_cfg;
    char msg[100];

    if (sidetnfs_probe_get_config_info(&probe) != SIDETNFS_PROBE_OK)
        return 0; /* no firmware detected -- expected offline case, no alert */

    if (probe.status == SIDETNFS_STATUS_OK && probe.protocol_version != SIDETNFS_CONFIG_PROTOCOL_VERSION) {
        sprintf(msg, "[3][Unsupported SideTNFS configuration protocol|Firmware: %lu|Application: %lu][OK]",
                probe.protocol_version, SIDETNFS_CONFIG_PROTOCOL_VERSION);
        form_alert(1, msg);
        return 0; /* Save stays blocked -- never write with mismatched semantics */
    }

    if (fetch_drive_config_from_firmware(&fw_cfg)) {
        *cfg = fw_cfg;
        return 1;
    }

    form_alert(1, "[3][SideTNFS firmware|Unexpected configuration.|Using built-in defaults.][OK]");
    return 0;
}

static int drives_match(const Drive *a, const Drive *b)
{
    if (a->state != b->state) return 0;
    if (a->state == DRIVE_SLOT_EMPTY) return 1; /* other fields are meaningless when EMPTY */

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
    default:
        break;
    }
    return 1;
}

/* Slots are fixed-position (never compacted/sorted), so a direct
 * index-by-index comparison is always valid -- no drive_count/sort
 * precondition needed anymore. */
static int drive_config_matches(const DriveConfig *a, const DriveConfig *b)
{
    int i;
    if (a->settings_letter != b->settings_letter) return 0;
    for (i = 0; i < MAX_DRIVES; i++)
        if (!drives_match(&a->drives[i], &b->drives[i]))
            return 0;
    return 1;
}

/* Fase 12B/AtariConfig-3: full firmware-backed Save. DELETE_DRIVE (==
 * SET_DRIVE state=EMPTY) for all 8 fixed slots first (so letter swaps
 * never hit a temporary duplicate -- a DISABLED slot reserves its
 * letter just as much as an ENABLED one), then SET_CONFIG_DRIVE for the
 * SETTINGS letter, then one SET_DRIVE per non-EMPTY slot with its exact
 * index and current state, then exactly one SAVE_CONFIG, then a full
 * readback + compare. Returns 1 only after the readback matches
 * exactly; msg is always filled in on failure. Never calls SAVE_CONFIG
 * if an earlier step failed -- flash is only ever touched by that one
 * call. No "too many drives" check needed anymore: the array is fixed
 * at exactly MAX_ORDINARY_DRIVES slots, so that failure mode is now
 * structurally impossible. */
static int save_to_firmware(const DriveConfig *cfg, char *msg)
{
    unsigned long status;
    int i;
    DriveConfig readback;

    for (i = 0; i < MAX_ORDINARY_DRIVES; i++) {
        if (sidetnfs_probe_delete_drive((unsigned long)i, &status) != SIDETNFS_PROBE_OK) {
            sprintf(msg, "[3][Save to firmware failed|DELETE_DRIVE(slot %d) timed out.|Nothing was saved.][OK]", i + 1);
            return 0;
        }
        if (status != SIDETNFS_STATUS_OK && status != SIDETNFS_STATUS_EMPTY_SLOT) {
            sprintf(msg, "[3][Save to firmware failed|DELETE_DRIVE(slot %d): %s|Nothing was saved.][OK]",
                    i + 1, sidetnfs_drive_status_text(status));
            return 0;
        }
    }

    if (sidetnfs_probe_set_config_drive((unsigned long)(unsigned char)cfg->settings_letter, &status) != SIDETNFS_PROBE_OK) {
        sprintf(msg, "[3][Save to firmware failed|SET_CONFIG_DRIVE timed out.|Nothing was saved.][OK]");
        return 0;
    }
    if (status != SIDETNFS_STATUS_OK) {
        sprintf(msg, "[3][Save to firmware failed|SETTINGS letter: %s|Nothing was saved.][OK]",
                sidetnfs_drive_status_text(status));
        return 0;
    }

    for (i = 0; i < MAX_ORDINARY_DRIVES; i++) {
        SideTnfsDriveInfo wire;
        const Drive *d = &cfg->drives[i];

        if (drive_slot_is_empty(d))
            continue; /* already cleared by the DELETE_DRIVE loop above */

        ui_to_wire_drive(d, &wire);
        if (sidetnfs_probe_set_drive((unsigned long)i, &wire, &status) != SIDETNFS_PROBE_OK) {
            sprintf(msg, "[3][Save to firmware failed|SET_DRIVE(slot %d) timed out.|Nothing was saved.][OK]", i + 1);
            return 0;
        }
        if (status != SIDETNFS_STATUS_OK) {
            sprintf(msg, "[3][Save to firmware failed|Slot %d (%c:): %s|Nothing was saved.][OK]",
                    i + 1, d->letter, sidetnfs_drive_status_text(status));
            return 0;
        }
    }

    if (sidetnfs_probe_save_config(&status) != SIDETNFS_PROBE_OK) {
        sprintf(msg, "[3][Save to firmware failed|SAVE_CONFIG timed out.|Nothing was saved.][OK]");
        return 0;
    }
    if (status != SIDETNFS_STATUS_OK) {
        sprintf(msg, "[3][Save to firmware failed|SAVE_CONFIG: %s][OK]", sidetnfs_drive_status_text(status));
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
/* Wire <-> UI RTC/NTP-config translation (Fase 12A)                    */
/* Explicit field-by-field, same reasoning as wire_to_ui_netconfig():   */
/* never memcpy() between the wire struct and the UI struct, even       */
/* though every field length happens to match numerically today.       */
/* ================================================================== */

static void wire_to_ui_rtcconfig(const SideTnfsRtcConfig *w, RtcConfig *rc)
{
    memset(rc, 0, sizeof(*rc));
    rc->ntp_enabled = (w->enabled != 0UL) ? 1 : 0;
    strncpy(rc->ntp_server, w->ntp_server, RTCCONFIG_NTP_SERVER_LEN - 1);
    strncpy(rc->utc_offset, w->utc_offset, RTCCONFIG_UTC_OFFSET_LEN - 1);
}

static void ui_to_wire_rtcconfig(const RtcConfig *rc, SideTnfsRtcConfig *w)
{
    memset(w, 0, sizeof(*w));
    w->enabled = (unsigned long)(rc->ntp_enabled ? 1 : 0);
    strncpy(w->ntp_server, rc->ntp_server, SIDETNFS_RTC_NTP_SERVER_LEN - 1);
    strncpy(w->utc_offset, rc->utc_offset, SIDETNFS_RTC_UTC_OFFSET_LEN - 1);
}

static const char *rtcconfig_status_text(unsigned long status)
{
    switch (status) {
    case SIDETNFS_RTCCONFIG_STATUS_OK:                  return "OK.";
    case SIDETNFS_RTCCONFIG_STATUS_INVALID_ENABLED:     return "Invalid NTP enabled setting.";
    case SIDETNFS_RTCCONFIG_STATUS_INVALID_NTP_SERVER:  return "Invalid NTP server.";
    case SIDETNFS_RTCCONFIG_STATUS_INVALID_UTC_OFFSET:  return "Invalid UTC offset.";
    case SIDETNFS_RTCCONFIG_STATUS_NOT_STAGED:          return "No config staged.";
    case SIDETNFS_RTCCONFIG_STATUS_FLASH_WRITE_FAILED:  return "Flash write failed.";
    case SIDETNFS_RTCCONFIG_STATUS_FLASH_VERIFY_FAILED: return "Flash verify failed.";
    default:                                            return "Unknown status.";
    }
}

/* Silent building block, same shape as fetch_network_config_from_firmware():
 * returns 1 on a fully consistent OK read, 0 on timeout or any non-OK
 * status -- *out is left untouched on failure. */
static int fetch_rtc_config_from_firmware(RtcConfig *out)
{
    SideTnfsRtcConfig wire;

    if (sidetnfs_probe_get_rtc_config(&wire) != SIDETNFS_PROBE_OK)
        return 0;
    if (wire.status != SIDETNFS_RTCCONFIG_STATUS_OK)
        return 0;

    wire_to_ui_rtcconfig(&wire, out);
    return 1;
}

/* Shared by rc_save_to_config() (live TEDINFO edit buffers, see the
 * Clock/NTP editor above) and validate_rtcconfig() (perform_save()'s
 * defensive re-check against the already-committed RtcConfig, once the
 * dialog itself is closed and those edit buffers are gone) -- one
 * validation rule set, exactly as asked: "use the existing CLOCK
 * validation" rather than a second, independently-written copy of it. */
static int rtcconfig_validate_fields(int enabled, const char *server, const char *offset, char *msg)
{
    const char *p;
    long off;
    char *end;

    if (enabled && !buf_nonempty(server)) {
        sprintf(msg, "[3][Validation error|NTP server is required.][OK]");
        return 0;
    }
    for (p = server; *p; p++) {
        if (*p == ' ') {
            sprintf(msg, "[3][Validation error|NTP server may not contain|spaces.][OK]");
            return 0;
        }
    }

    off = strtol(offset, &end, 10);
    if (offset[0] == '\0' || *end != '\0' || off < -12 || off > 14) {
        sprintf(msg, "[3][Validation error|UTC offset must be a whole|number from -12 to +14.][OK]");
        return 0;
    }

    return 1;
}

/* Field-by-field local validation, mirroring validate_netconfig()'s
 * shape -- the same defensive re-check pattern applied at Save time. */
static int validate_rtcconfig(const RtcConfig *rc, char *msg)
{
    return rtcconfig_validate_fields(rc->ntp_enabled, rc->ntp_server, rc->utc_offset, msg);
}

/* 1 if any field differs -- drives Save's "only save RTC config if the
 * user actually changed it" requirement, same idiom as netconfig_changed(). */
static int rtcconfig_changed(const RtcConfig *a, const RtcConfig *b)
{
    if (a->ntp_enabled != b->ntp_enabled) return 1;
    if (strcmp(a->ntp_server, b->ntp_server) != 0) return 1;
    if (strcmp(a->utc_offset, b->utc_offset) != 0) return 1;
    return 0;
}

/* Fase 12A: SET_RTC_CONFIG, then SAVE_RTC_CONFIG, then a full
 * GET_RTC_CONFIG readback + compare -- same "stop at the first failure,
 * never partially applied" shape as save_network_to_firmware(). Never
 * calls SAVE_RTC_CONFIG if SET_RTC_CONFIG failed; on any failure *rc
 * (the caller's live g_rtcconfig) is never touched here. */
static int save_rtc_to_firmware(const RtcConfig *rc, char *msg)
{
    SideTnfsRtcConfig wire;
    unsigned long status;
    RtcConfig readback;

    ui_to_wire_rtcconfig(rc, &wire);

    if (sidetnfs_probe_set_rtc_config(&wire, &status) != SIDETNFS_PROBE_OK) {
        sprintf(msg, "[3][Clock save failed|SET_RTC_CONFIG timed out.|Nothing was saved.][OK]");
        return 0;
    }
    if (status != SIDETNFS_RTCCONFIG_STATUS_OK) {
        sprintf(msg, "[3][Clock save failed|%s|Nothing was saved.][OK]", rtcconfig_status_text(status));
        return 0;
    }

    if (sidetnfs_probe_save_rtc_config(&status) != SIDETNFS_PROBE_OK) {
        sprintf(msg, "[3][Clock save failed|SAVE_RTC_CONFIG timed out.|Nothing was saved.][OK]");
        return 0;
    }
    if (status != SIDETNFS_RTCCONFIG_STATUS_OK) {
        sprintf(msg, "[3][Clock save failed|%s][OK]", rtcconfig_status_text(status));
        return 0;
    }

    if (!fetch_rtc_config_from_firmware(&readback)) {
        sprintf(msg, "[3][Clock save verification failed|Could not read back|the saved configuration.][OK]");
        return 0;
    }
    if (rtcconfig_changed(rc, &readback)) {
        sprintf(msg, "[3][Clock save verification failed|Flash contents do not match|the edited settings.][OK]");
        return 0;
    }

    return 1;
}

/* Fase 12A: startup counterpart to network_startup_load(), called right
 * after it. A timeout OR a non-OK status both get the same silent
 * fallback to the built-in defaults (rtcconfig_init_defaults(), set by
 * shared_fields_init()) -- unlike network_startup_load(), this never
 * alerts on a non-OK status either: firmware that predates Fase 12A
 * (RTC_CONFIG unsupported) is an expected, normal condition here, not
 * an error worth interrupting startup for. Either way
 * g_rtcconfig_baseline is set equal to g_rtcconfig afterwards, so Save
 * does not fire a spurious RTC save for a session that never touched
 * the Clock dialog. */
static void rtc_startup_load(void)
{
    SideTnfsRtcConfig wire;

    if (sidetnfs_probe_get_rtc_config(&wire) == SIDETNFS_PROBE_OK
        && wire.status == SIDETNFS_RTCCONFIG_STATUS_OK) {
        wire_to_ui_rtcconfig(&wire, &g_rtcconfig);
    }
    g_rtcconfig_baseline = g_rtcconfig;
}

/* ================================================================== */
/* SETTINGS disk letter editor                                         */
/* Only the drive letter is editable; nickname/type are fixed context. */
/* No Test, no Delete/Remove -- the SETTINGS disk always exists and is  */
/* never one of the eight ordinary slots (drive.h).                     */
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
    cl_dlg[CL_TITLE].ob_spec.free_string = "SETTINGS Disk Letter";

    set_obj(cl_dlg, CL_DIV1, G_BOX, NONE, NORMAL, cw, ydiv1, DW - 2*cw, 2);
    cl_dlg[CL_DIV1].ob_spec.index = 0x00001171L;

    set_obj(cl_dlg, CL_LBL_NICK, G_STRING, NONE, NORMAL, xl, ynick, 11*cw, rh);
    cl_dlg[CL_LBL_NICK].ob_spec.free_string = "Nickname:";
    set_obj(cl_dlg, CL_VAL_NICK, G_STRING, NONE, NORMAL, xf, ynick, 23*cw, rh);
    cl_dlg[CL_VAL_NICK].ob_spec.free_string = "SETTINGS disk";

    set_obj(cl_dlg, CL_LBL_TYPE, G_STRING, NONE, NORMAL, xl, ytype, 11*cw, rh);
    cl_dlg[CL_LBL_TYPE].ob_spec.free_string = "Type:";
    set_obj(cl_dlg, CL_VAL_TYPE, G_STRING, NONE, NORMAL, xf, ytype, 10*cw, rh);
    cl_dlg[CL_VAL_TYPE].ob_spec.free_string = "SETTINGS";

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

/* Applies the new letter to cfg->settings_letter on OK; leaves it
 * untouched on Cancel. Validates against the eight ordinary slots only,
 * scanning for the specific conflicting slot number to report -- never
 * against cfg->settings_letter itself, which would always "conflict"
 * with its own current value. */
static void cl_editor_run(DriveConfig *cfg)
{
    short x, y, w, h;
    short which;
    int done;
    char msg[100];
    char newletter;
    char drv[2];
    int i;

    cl_dialog_init();

    drv[0] = cfg->settings_letter; drv[1] = '\0';
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
            if (newletter < 'A' || newletter > 'Z' || newletter == 'A' || newletter == 'B') {
                sprintf(msg, "[3][Validation error|Drive %c: invalid letter.][OK]", newletter);
                form_alert(1, msg);
                break;
            }
            for (i = 0; i < MAX_DRIVES && !(drive_slot_is_configured(&cfg->drives[i]) && cfg->drives[i].letter == newletter); i++)
                ;
            if (i < MAX_DRIVES) {
                sprintf(msg, "[3][Validation error|Drive letter %c: is already used|by slot %d.][OK]", newletter, i + 1);
                form_alert(1, msg);
                break;
            }
            cfg->settings_letter = newletter;
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
    int yt, ydiv1, ydrv, ynick, ypath, yactive, ydiv2, ybtn;

    graf_handle(&cw, &ch, &bw, &bh);
    wind_get(0, WF_WORKXYWH, &sx, &sy, &sw, &sh);
    if (sh <= 0 || sh > 800) sh = 200;

    rh    = (sh >= 350) ? (int)ch : ((ch > 8) ? ch / 2 : (int)ch);
    tm    = (rh / 4 > 2) ? rh / 4 : 2;
    pitch = rh + 3;

    DW = 46 * cw;
    xl = 2 * cw;
    xf = 13 * cw;

    yt      = tm;
    ydiv1   = yt + rh + 1;
    ydrv    = ydiv1 + 5;
    ynick   = ydrv + pitch;
    ypath   = ynick + pitch;
    yactive = ypath + pitch;
    ydiv2   = yactive + rh + 2;
    ybtn    = ydiv2 + 7;
    DH      = ybtn + rh + tm + 3;

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

    set_obj(sd_dlg, SD_LBL_ACTIVE, G_STRING, NONE, NORMAL, xl, yactive, 11*cw, rh);
    sd_dlg[SD_LBL_ACTIVE].ob_spec.free_string = "Status:";
    set_obj(sd_dlg, SD_ACTIVE_BTN, G_BUTTON, EXIT | TOUCHEXIT, NORMAL, xf, yactive, 10*cw, rh);
    sd_dlg[SD_ACTIVE_BTN].ob_spec.free_string = buf_drive_active;

    set_obj(sd_dlg, SD_DIV2, G_BOX, NONE, NORMAL, cw, ydiv2, DW - 2*cw, 2);
    sd_dlg[SD_DIV2].ob_spec.index = 0x00001171L;

    set_obj(sd_dlg, SD_DELETE, G_BUTTON, EXIT | TOUCHEXIT, show_delete ? NORMAL : DISABLED,
            4*cw, ybtn, 9*cw, rh);
    sd_dlg[SD_DELETE].ob_spec.free_string = " Remove  ";

    set_obj(sd_dlg, SD_OK, G_BUTTON, EXIT | DEFAULT | TOUCHEXIT, NORMAL, 17*cw, ybtn, 8*cw, rh);
    sd_dlg[SD_OK].ob_spec.free_string = "   OK   ";

    set_obj(sd_dlg, SD_CANCEL, G_BUTTON, EXIT | TOUCHEXIT, NORMAL, 28*cw, ybtn, 8*cw, rh);
    sd_dlg[SD_CANCEL].ob_spec.free_string = " Cancel ";

    wire_tree(sd_dlg, SD_NOBJS);

    (void)sx; (void)sy; (void)sw; (void)bw; (void)bh;
}

static void sd_load_from_drive(const Drive *d, int is_new)
{
    char drv[2];
    drv[0] = d->letter; drv[1] = '\0'; /* already set by the caller, either from drive_config_suggest_letter() or the existing record */
    set_buf(buf_te_drive, TE_BUF_DRV, drv);
    set_buf(buf_te_nick,  TE_BUF_NICK, d->nickname);
    set_buf(buf_sd_path,  TE_BUF_SDPATH, d->sd_path);

    /* A new (EMPTY) slot defaults to Active/ENABLED unless the user
     * explicitly toggles it off before OK -- see the report. */
    drive_editor_enabled = is_new ? 1 : (d->state == DRIVE_SLOT_ENABLED);
    update_drive_active_button_text();
}

static void sd_save_to_drive(Drive *d)
{
    char c = buf_te_drive[0];
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    d->letter = c;
    buf_copy(buf_te_nick, d->nickname, DRIVE_NICK_LEN);
    buf_copy(buf_sd_path, d->sd_path, DRIVE_SDPATH_LEN);
    d->type  = DRIVE_TYPE_SD;
    d->state = drive_editor_enabled ? DRIVE_SLOT_ENABLED : DRIVE_SLOT_DISABLED;
}

/* Returns 2 if the drive was removed (cleared to EMPTY), 1 if
 * added/modified, 0 if cancelled without changes. index is always a
 * valid fixed slot 0..MAX_DRIVES-1 -- "add" now means editing a slot
 * that is currently EMPTY, not appending to a compacted list. */
static int sd_editor_run(DriveConfig *cfg, int index)
{
    short x, y, w, h;
    short which;
    int done;
    int is_new = drive_slot_is_empty(&cfg->drives[index]);
    Drive working;
    char msg[100];

    if (is_new) {
        memset(&working, 0, sizeof(working));
        working.type   = DRIVE_TYPE_SD;
        working.letter = drive_config_suggest_letter(cfg);
    } else {
        working = cfg->drives[index];
    }

    sd_dialog_init(!is_new);
    sd_load_from_drive(&working, is_new);

    form_center(sd_dlg, &x, &y, &w, &h);
    form_dial(FMD_START, x, y, w, h, x, y, w, h);
    objc_draw(sd_dlg, SD_ROOT, MAX_DEPTH, x, y, w, h);

    done = 0;
    while (!done) {
        which = (short)(form_do(sd_dlg, SD_DRIVE_EDIT) & 0x7FFF);
        wait_mouse_release();

        switch (which) {
        case SD_ACTIVE_BTN:
            drive_editor_enabled = !drive_editor_enabled;
            update_drive_active_button_text();
            objc_draw(sd_dlg, SD_ROOT, MAX_DEPTH, x, y, w, h);
            break;

        case SD_DELETE:
            if (!is_new) {
                if (form_alert(1, "[2][Remove this drive configuration?][Remove|Cancel]") == 1)
                    done = 2;
            }
            break;

        case SD_OK:
            sd_save_to_drive(&working);
            if (!validate_drive(&working, cfg, index, msg)) {
                form_alert(1, msg);
                break;
            }
            cfg->drives[index] = working;
            done = 1;
            break;

        case SD_CANCEL:
        default:
            done = 3;
            break;
        }
    }

    form_dial(FMD_FINISH, x, y, w, h, x, y, w, h);

    if (done == 2) {
        memset(&cfg->drives[index], 0, sizeof(cfg->drives[index])); /* state == DRIVE_SLOT_EMPTY */
        return 2;
    }
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
    int yt, ydiv1, ydrv, ynick, yhost, yport, ytrans, ymount, ymounthint, yactive, ydiv2, ybtn;

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
    yactive    = ymounthint + pitch;
    ydiv2      = yactive + rh + 2;
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

    set_obj(te_dlg, TE_LBL_ACTIVE, G_STRING, NONE, NORMAL, xl, yactive, 11*cw, rh);
    te_dlg[TE_LBL_ACTIVE].ob_spec.free_string = "Status:";
    set_obj(te_dlg, TE_ACTIVE_BTN, G_BUTTON, EXIT | TOUCHEXIT, NORMAL, xf, yactive, 10*cw, rh);
    te_dlg[TE_ACTIVE_BTN].ob_spec.free_string = buf_drive_active;

    set_obj(te_dlg, TE_DIV2, G_BOX, NONE, NORMAL, cw, ydiv2, DW - 2*cw, 2);
    te_dlg[TE_DIV2].ob_spec.index = 0x00001171L;

    set_obj(te_dlg, TE_TEST, G_BUTTON, EXIT | TOUCHEXIT, NORMAL, 2*cw, ybtn, 8*cw, rh);
    te_dlg[TE_TEST].ob_spec.free_string = "  Test  ";

    set_obj(te_dlg, TE_DELETE, G_BUTTON, EXIT | TOUCHEXIT, show_delete ? NORMAL : DISABLED,
            13*cw, ybtn, 9*cw, rh);
    te_dlg[TE_DELETE].ob_spec.free_string = " Remove  ";

    set_obj(te_dlg, TE_OK, G_BUTTON, EXIT | DEFAULT | TOUCHEXIT, NORMAL, 25*cw, ybtn, 8*cw, rh);
    te_dlg[TE_OK].ob_spec.free_string = "   OK   ";

    set_obj(te_dlg, TE_CANCEL, G_BUTTON, EXIT | TOUCHEXIT, NORMAL, 36*cw, ybtn, 8*cw, rh);
    te_dlg[TE_CANCEL].ob_spec.free_string = " Cancel ";

    wire_tree(te_dlg, TE_NOBJS);

    (void)sx; (void)sy; (void)sw; (void)bw; (void)bh;
}

static void te_load_from_drive(const Drive *d, int is_new)
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

    /* A new (EMPTY) slot defaults to Active/ENABLED unless the user
     * explicitly toggles it off before OK -- see the report. */
    drive_editor_enabled = is_new ? 1 : (d->state == DRIVE_SLOT_ENABLED);
    update_drive_active_button_text();
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
    d->type  = DRIVE_TYPE_TNFS;
    d->state = drive_editor_enabled ? DRIVE_SLOT_ENABLED : DRIVE_SLOT_DISABLED;
}

/* Returns 2 if the drive was removed (cleared to EMPTY), 1 if
 * added/modified, 0 if cancelled without changes. index is always a
 * valid fixed slot 0..MAX_DRIVES-1 -- "add" now means editing a slot
 * that is currently EMPTY, not appending to a compacted list. */
static int tnfs_editor_run(DriveConfig *cfg, int index)
{
    short x, y, w, h;
    short which;
    int done;
    int is_new = drive_slot_is_empty(&cfg->drives[index]);
    Drive working;
    char msg[220];

    if (is_new) {
        memset(&working, 0, sizeof(working));
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
    te_load_from_drive(&working, is_new);

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

        case TE_ACTIVE_BTN:
            drive_editor_enabled = !drive_editor_enabled;
            update_drive_active_button_text();
            objc_draw(te_dlg, TE_ROOT, MAX_DEPTH, x, y, w, h);
            break;

        case TE_DELETE:
            if (!is_new) {
                if (form_alert(1, "[2][Remove this drive configuration?][Remove|Cancel]") == 1)
                    done = 2;
            }
            break;

        case TE_OK:
            te_save_to_drive(&working);
            if (!validate_drive(&working, cfg, index, msg)) {
                form_alert(1, msg);
                break;
            }
            cfg->drives[index] = working;
            done = 1;
            break;

        case TE_CANCEL:
        default:
            done = 3;
            break;
        }
    }

    form_dial(FMD_FINISH, x, y, w, h, x, y, w, h);

    if (done == 2) {
        memset(&cfg->drives[index], 0, sizeof(cfg->drives[index])); /* state == DRIVE_SLOT_EMPTY */
        return 2;
    }
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
/* Clock/NTP editor (Fase 12, v1 -- KISS)                               */
/* UI only, same shape as the WiFi/Network editor below: OK validates   */
/* and copies the edit buffers into *rc; Cancel/ESC leaves *rc exactly  */
/* as it was on entry. No firmware read/write yet -- see the SAVE TODO  */
/* in perform_save(). */
/* ================================================================== */

static void rc_dialog_init(void)
{
    short cw, ch, bw, bh;
    short sx, sy, sw, sh;
    int rh, tm, pitch;
    int DW, DH, xl, xf;
    int yt, ydiv1, yntp, yserver, yoffset, ydiv2, ybtn;

    graf_handle(&cw, &ch, &bw, &bh);
    wind_get(0, WF_WORKXYWH, &sx, &sy, &sw, &sh);
    if (sh <= 0 || sh > 800) sh = 200;

    rh    = (sh >= 350) ? (int)ch : ((ch > 8) ? ch / 2 : (int)ch);
    tm    = (rh / 4 > 2) ? rh / 4 : 2;
    pitch = rh + 3;

    DW = 54 * cw;
    xl = 2 * cw;
    xf = 29 * cw; /* past "Set Atari clock using NTP:" (26 chars) + margin */

    yt      = tm;
    ydiv1   = yt + rh + 1;
    yntp    = ydiv1 + 5;
    yserver = yntp + pitch;
    yoffset = yserver + pitch;
    ydiv2   = yoffset + rh + 2;
    ybtn    = ydiv2 + 7;
    DH      = ybtn + rh + tm + 3;

    set_obj(rc_dlg, RC_ROOT, G_BOX, NONE, NORMAL, 0, 0, DW, DH);
    rc_dlg[RC_ROOT].ob_spec.index = 0x00031070L;

    set_obj(rc_dlg, RC_TITLE, G_STRING, NONE, NORMAL, 20*cw, yt, 14*cw, rh);
    rc_dlg[RC_TITLE].ob_spec.free_string = "Clock / NTP";

    set_obj(rc_dlg, RC_DIV1, G_BOX, NONE, NORMAL, cw, ydiv1, DW - 2*cw, 2);
    rc_dlg[RC_DIV1].ob_spec.index = 0x00001171L;

    set_obj(rc_dlg, RC_LBL_NTP, G_STRING, NONE, NORMAL, xl, yntp, 26*cw, rh);
    rc_dlg[RC_LBL_NTP].ob_spec.free_string = "Set Atari clock using NTP:";
    set_obj(rc_dlg, RC_NTP_BTN, G_BUTTON, EXIT | TOUCHEXIT, NORMAL, xf, yntp, 8*cw, rh);
    rc_dlg[RC_NTP_BTN].ob_spec.free_string = buf_rc_ntp_val;

    set_obj(rc_dlg, RC_LBL_SERVER, G_STRING, NONE, NORMAL, xl, yserver, 26*cw, rh);
    rc_dlg[RC_LBL_SERVER].ob_spec.free_string = "NTP server:";
    set_obj(rc_dlg, RC_SERVER_EDIT, G_FBOXTEXT, EDITABLE, NORMAL, xf, yserver, 20*cw, rh);
    rc_dlg[RC_SERVER_EDIT].ob_spec.tedinfo = &ti_rc_server;

    set_obj(rc_dlg, RC_LBL_OFFSET, G_STRING, NONE, NORMAL, xl, yoffset, 26*cw, rh);
    rc_dlg[RC_LBL_OFFSET].ob_spec.free_string = "UTC offset:";
    set_obj(rc_dlg, RC_OFFSET_EDIT, G_FBOXTEXT, EDITABLE, NORMAL, xf, yoffset, 4*cw, rh);
    rc_dlg[RC_OFFSET_EDIT].ob_spec.tedinfo = &ti_rc_offset;

    set_obj(rc_dlg, RC_DIV2, G_BOX, NONE, NORMAL, cw, ydiv2, DW - 2*cw, 2);
    rc_dlg[RC_DIV2].ob_spec.index = 0x00001171L;

    set_obj(rc_dlg, RC_OK, G_BUTTON, EXIT | DEFAULT | TOUCHEXIT, NORMAL, 17*cw, ybtn, 8*cw, rh);
    rc_dlg[RC_OK].ob_spec.free_string = "   OK   ";

    set_obj(rc_dlg, RC_CANCEL, G_BUTTON, EXIT | TOUCHEXIT, NORMAL, 28*cw, ybtn, 8*cw, rh);
    rc_dlg[RC_CANCEL].ob_spec.free_string = " Cancel ";

    wire_tree(rc_dlg, RC_NOBJS);

    (void)sx; (void)sy; (void)sw; (void)bw; (void)bh;
}

static void rc_update_ntp_button_text(void)
{
    strncpy(buf_rc_ntp_val, rc_ntp_enabled ? "  Yes   " : "  No    ", RC_BUF_NTP_VAL - 1);
    buf_rc_ntp_val[RC_BUF_NTP_VAL - 1] = '\0';
}

static void rc_load_from_config(const RtcConfig *rc)
{
    rc_ntp_enabled = rc->ntp_enabled;
    rc_update_ntp_button_text();
    set_buf(buf_rc_server, RC_BUF_SERVER, rc->ntp_server);
    set_buf(buf_rc_offset, RC_BUF_OFFSET, rc->utc_offset);
}

/* Returns 1 on success (fields copied into *rc), 0 on a validation
 * failure -- *rc is left untouched on failure, same "reject and stay
 * open" pattern as nc_save_to_config(). Normalizes utc_offset to the
 * canonical form ("0", "+1", "-5", "+14") regardless of how the user
 * typed it (a leading '+' is optional on input). */
static int rc_save_to_config(RtcConfig *rc)
{
    char server[RC_BUF_SERVER];
    char offset[RC_BUF_OFFSET];
    char msg[100];
    long off;
    char *end;

    /* buf_copy() strips the trailing spaces GEM pads editable fields
     * with, same as every other text field in this file. */
    buf_copy(buf_rc_server, server, sizeof(server));
    buf_copy(buf_rc_offset, offset, sizeof(offset));

    /* Same validation rtcconfig_validate_fields() also runs at Save
     * time against the committed RtcConfig (validate_rtcconfig()) --
     * one rule set, not two independently-written copies of it. */
    if (!rtcconfig_validate_fields(rc_ntp_enabled, server, offset, msg)) {
        form_alert(1, msg);
        return 0;
    }

    /* strtol() accepts an optional leading '+'/'-', matching the
     * accepted input notation exactly; already range/format-checked by
     * rtcconfig_validate_fields() above. */
    off = strtol(offset, &end, 10);
    (void)end;

    rc->ntp_enabled = rc_ntp_enabled;
    strncpy(rc->ntp_server, server, RTCCONFIG_NTP_SERVER_LEN - 1);
    rc->ntp_server[RTCCONFIG_NTP_SERVER_LEN - 1] = '\0';

    if (off == 0)
        strncpy(rc->utc_offset, "0", RTCCONFIG_UTC_OFFSET_LEN - 1);
    else
        sprintf(rc->utc_offset, "%+ld", off);
    rc->utc_offset[RTCCONFIG_UTC_OFFSET_LEN - 1] = '\0';

    return 1;
}

static void rtc_editor_run(RtcConfig *rc)
{
    short x, y, w, h;
    short which;
    short start_obj;
    int done;

    rc_dialog_init();
    rc_load_from_config(rc);

    form_center(rc_dlg, &x, &y, &w, &h);
    form_dial(FMD_START, x, y, w, h, x, y, w, h);
    objc_draw(rc_dlg, RC_ROOT, MAX_DEPTH, x, y, w, h);

    done = 0;
    start_obj = RC_SERVER_EDIT;
    while (!done) {
        which = (short)(form_do(rc_dlg, start_obj) & 0x7FFF);
        wait_mouse_release();
        start_obj = RC_SERVER_EDIT;

        switch (which) {
        case RC_NTP_BTN:
            rc_ntp_enabled = !rc_ntp_enabled;
            rc_update_ntp_button_text();
            /* Full-tree redraw, same choice as NC_DHCP_BTN/NC_STATIC_BTN:
             * this dialog is small enough that the flicker is a non-issue,
             * and it sidesteps the G_STRING/G_BUTTON background-clear
             * pitfall documented at nc_redraw_auth_val() entirely. */
            objc_draw(rc_dlg, RC_ROOT, MAX_DEPTH, x, y, w, h);
            break;

        case RC_OK:
            if (!rc_save_to_config(rc)) {
                start_obj = RC_SERVER_EDIT;
                break;
            }
            done = 1;
            break;

        case RC_CANCEL:
        default:
            done = 1;
            break;
        }
    }

    form_dial(FMD_FINISH, x, y, w, h, x, y, w, h);
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

    set_obj(nc_dlg, NC_CLOCK_BTN, G_BUTTON, EXIT | TOUCHEXIT, NORMAL, 38*cw, ybtn, 9*cw, rh);
    nc_dlg[NC_CLOCK_BTN].ob_spec.free_string = "  Clock  ";

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

        case NC_CLOCK_BTN:
            /* Fully nested modal, same proven shape as SW_CONFIG opening
             * this very dialog from the status window: rtc_editor_run()
             * owns its own complete form_dial(FMD_START)/form_do()/
             * form_dial(FMD_FINISH) cycle, so no state is left hanging
             * here -- the explicit NC_ROOT redraw below is what restores
             * this dialog afterward. */
            rtc_editor_run(&g_rtcconfig);
            objc_draw(nc_dlg, NC_ROOT, MAX_DEPTH, x, y, w, h);
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
    int yt, ydiv1, ysettings, ydiv2, yrow0, ydiv3, ybtn;
    int i;

    graf_handle(&cw, &ch, &bw, &bh);
    wind_get(0, WF_WORKXYWH, &sx, &sy, &sw, &sh);
    if (sh <= 0 || sh > 800) sh = 200;

    rh        = (sh >= 350) ? (int)ch : ((ch > 8) ? ch / 2 : (int)ch);
    tm        = (rh / 4 > 2) ? rh / 4 : 2;
    row_pitch = rh + 2;

    DW        = 48 * cw;
    yt        = tm;
    ydiv1     = yt + rh + 1;
    ysettings = ydiv1 + 5;
    ydiv2     = ysettings + rh + 2;
    yrow0     = ydiv2 + 5;
    ydiv3     = yrow0 + MAX_DRIVES * row_pitch + 2;
    ybtn      = ydiv3 + 7;
    DH        = ybtn + rh + tm + 3;

    set_obj(ov_dlg, OV_ROOT, G_BOX, NONE, NORMAL, 0, 0, DW, DH);
    ov_dlg[OV_ROOT].ob_spec.index = 0x00031070L;

    set_obj(ov_dlg, OV_TITLE, G_STRING, NONE, NORMAL, 14*cw, yt, 20*cw, rh);
    ov_dlg[OV_TITLE].ob_spec.free_string = "SideTNFS Drives";

    set_obj(ov_dlg, OV_DIV1, G_BOX, NONE, NORMAL, cw, ydiv1, DW - 2*cw, 2);
    ov_dlg[OV_DIV1].ob_spec.index = 0x00001171L;

    set_obj(ov_dlg, OV_LBL_SETTINGS, G_STRING, NONE, NORMAL, cw, ysettings, 14*cw, rh);
    ov_dlg[OV_LBL_SETTINGS].ob_spec.free_string = "SETTINGS disk:";
    set_obj(ov_dlg, OV_SETTINGS_VAL, G_STRING, NONE, NORMAL, 15*cw, ysettings, 20*cw, rh);
    ov_dlg[OV_SETTINGS_VAL].ob_spec.free_string = ov_settings_val;
    set_obj(ov_dlg, OV_SETTINGS_BTN, G_BUTTON, EXIT | TOUCHEXIT, NORMAL, 36*cw, ysettings, 8*cw, rh);
    ov_dlg[OV_SETTINGS_BTN].ob_spec.free_string = " Edit ";

    set_obj(ov_dlg, OV_DIV2, G_BOX, NONE, NORMAL, cw, ydiv2, DW - 2*cw, 2);
    ov_dlg[OV_DIV2].ob_spec.index = 0x00001171L;

    for (i = 0; i < MAX_DRIVES; i++) {
        int ry = yrow0 + i * row_pitch;

        set_obj(ov_dlg, OV_ROW_TEXT(i), G_STRING, NONE, NORMAL, cw, ry, 34*cw, rh);
        ov_dlg[OV_ROW_TEXT(i)].ob_spec.free_string = ov_row_text[i];

        set_obj(ov_dlg, OV_ROW_BTN(i), G_BUTTON, EXIT | TOUCHEXIT, NORMAL, 36*cw, ry, 8*cw, rh);
        ov_dlg[OV_ROW_BTN(i)].ob_spec.free_string = "  Add   ";
    }

    set_obj(ov_dlg, OV_DIV3, G_BOX, NONE, NORMAL, cw, ydiv3, DW - 2*cw, 2);
    ov_dlg[OV_DIV3].ob_spec.index = 0x00001171L;

    set_obj(ov_dlg, OV_OK, G_BUTTON, EXIT | DEFAULT | TOUCHEXIT, NORMAL, 19*cw, ybtn, 8*cw, rh);
    ov_dlg[OV_OK].ob_spec.free_string = "   OK   ";

    set_obj(ov_dlg, OV_CANCEL, G_BUTTON, EXIT | TOUCHEXIT, NORMAL, 30*cw, ybtn, 8*cw, rh);
    ov_dlg[OV_CANCEL].ob_spec.free_string = " Cancel ";

    wire_tree(ov_dlg, OV_NOBJS);

    (void)sx; (void)sy; (void)sw; (void)bw; (void)bh;
}

/* Eight fixed rows, always -- an EMPTY slot shows only its slot number
 * ("Empty", no stale letter/type/nickname left visible) and an "Add"
 * button; a configured slot shows slot/state/letter/type/nickname and
 * an "Edit" button. Never reordered. */
static void ov_refresh_rows(const DriveConfig *cfg)
{
    int i;

    sprintf(ov_settings_val, "%c:", cfg->settings_letter);

    for (i = 0; i < MAX_DRIVES; i++) {
        const Drive *d = &cfg->drives[i];

        if (drive_slot_is_empty(d)) {
            sprintf(ov_row_text[i], "%d  Empty", i + 1);
            ov_dlg[OV_ROW_BTN(i)].ob_spec.free_string = "  Add   ";
        } else {
            const char *state_word  = drive_slot_is_enabled(d) ? "Active" : "Inactive";
            const char *type_abbrev = (d->type == DRIVE_TYPE_TNFS) ? "TNFS" : "SD";
            sprintf(ov_row_text[i], "%d  %-8s %c:  %-4s %-20.20s",
                    i + 1, state_word, d->letter, type_abbrev, d->nickname);
            ov_dlg[OV_ROW_BTN(i)].ob_spec.free_string = "  Edit  ";
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

/* Fase 9B: explicit, testable restart decision. Every block below is
 * proven -- not assumed -- to only take effect after a genuine restart:
 * sidetnfs_config_save()'s own doc comment ("writing flash alone never
 * changes the currently active TNFS session/drive letter/open
 * handles/DTAs; a reboot is required for that"), and the network/RTC
 * SAVE handlers' equivalent comments ("never touches WiFi/NTP/reboots
 * -- the new configuration only takes effect on the next normal Pico
 * boot"). No field within any one block is live-applied, so block-level
 * "did it actually change" is sufficient -- nothing more granular is
 * needed, and nothing here is a guess. */
static int configuration_changes_require_restart(int drives_changed, int network_changed, int rtc_changed)
{
    return drives_changed || network_changed || rtc_changed;
}

/* Fase 9B: ~150ms after its ACK, the Pico itself starts rebooting (see
 * sidetnfs_probe_reboot_pico()'s own comment) -- this is a fixed,
 * bounded wait for that to actually happen before the Atari resets too,
 * not a busy-loop: Vsync() blocks until the next vertical blank, same
 * convention as every other bounded wait in this program
 * (PAL_VBLS_PER_SEC, sidetnfs_probe.c). 15 VBLs is ~300ms at 50Hz
 * (PAL) and ~250ms at 60Hz (NTSC) -- within the requested 200-300ms
 * margin either way. */
#define REBOOT_PICO_SETTLE_VBLS 15

/* Fase 9B: runs after a fully successful Save (drives, and network/RTC
 * if changed) -- baselines are already updated at this point regardless
 * of what the user chooses here (see perform_save()), so Later can
 * never leave the dirty-state/Quit-warning out of sync with what was
 * actually persisted. Never calls any SAVE/SET command itself -- only
 * sidetnfs_probe_reboot_pico(), at most once. */
static void offer_restart_if_needed(int restart_required)
{
    if (!restart_required) {
        form_alert(1, "[1][Settings saved.][OK]");
        return;
    }

    /* Later is the safe default (button 2) even though Restart is
     * listed first, per the report -- accidentally hitting Return must
     * never trigger an unattended Pico reboot + Atari reset. */
    if (form_alert(2, "[1][Settings saved.|Restart SideTNFS and Atari|"
                      "to apply the changes?][Restart|Later]") == 1) {
        int i;

        if (sidetnfs_probe_reboot_pico() != SIDETNFS_PROBE_OK) {
            /* Timeout covers both "no firmware response" and "firmware
             * too old to recognize REBOOT_PICO" -- either way, no ACK
             * was ever seen, so the Atari must not reset: nothing on
             * the Pico side is known to be restarting. */
            form_alert(1, "[3][Restart failed|(no response from SideTNFS).|"
                          "Settings are saved and will become|"
                          "active after a manual restart.][OK]");
            return;
        }

        for (i = 0; i < REBOOT_PICO_SETTLE_VBLS; i++)
            Vsync();
        Supexec(atari_do_reset);
    } else {
        form_alert(1, "[1][Settings saved.|They will become active after|"
                      "the next restart.][OK]");
    }
}

/* The only place that ever writes to firmware: the status window's own
 * SAVE button (SW_SAVE). Covers the drive list and, if the user
 * actually changed them, the wifi/network config and the RTC/NTP
 * config (separate SET/SAVE commands each, see
 * save_to_firmware()/save_network_to_firmware()/save_rtc_to_firmware()).
 * Stops at the first failure; a failed network or RTC save leaves
 * g_netconfig/g_rtcconfig exactly as the user left them (never silently
 * replaced), and anything already saved before it stays saved
 * regardless. Returns 1 if the caller's window should close afterwards
 * (everything that changed is fully saved and read back -- a reboot is
 * needed before any of it takes effect, so there is nothing left to do
 * in this run), 0 to keep it open. */
static int perform_save(DriveConfig *cfg, int firmware_backed)
{
    int i;
    int all_valid = 1;
    char msg[220];
    int drives_changed;
    int network_changed;
    int rtc_changed;
    int restart_required;

    /* Captured before save_to_firmware() runs (and before it overwrites
     * g_driveconfig_baseline on success) -- the restart decision must
     * reflect what actually changed relative to the *previous* baseline,
     * never a post-save diff against itself (which would always read
     * "unchanged"). See the report. */
    drives_changed = !drive_config_matches(cfg, &g_driveconfig_baseline);

    /* No "too many drives" check needed: the array is fixed at exactly
     * MAX_ORDINARY_DRIVES slots, so that failure mode is structurally
     * impossible now. EMPTY slots carry no fields to validate. */
    for (i = 0; i < MAX_DRIVES; i++) {
        if (drive_slot_is_empty(&cfg->drives[i]))
            continue;
        if (!validate_drive(&cfg->drives[i], cfg, i, msg)) {
            form_alert(1, msg);
            all_valid = 0;
            break;
        }
    }

    /* Only validate/save network (or RTC) config if the user actually
     * changed it since it was loaded (or last saved) -- opening/closing
     * the Config or Clock dialog without real edits must never trigger
     * a save. */
    network_changed = netconfig_changed(&g_netconfig, &g_netconfig_baseline);
    if (all_valid && network_changed) {
        if (!validate_netconfig(&g_netconfig, msg)) {
            form_alert(1, msg);
            all_valid = 0;
        }
    }

    rtc_changed = rtcconfig_changed(&g_rtcconfig, &g_rtcconfig_baseline);
    if (all_valid && rtc_changed) {
        if (!validate_rtcconfig(&g_rtcconfig, msg)) {
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

    /* Drives first (existing, proven flow); network and RTC after,
     * each only if actually changed. Stop at the first failure. */
    if (!save_to_firmware(cfg, msg)) {
        form_alert(1, msg);
        return 0;
    }
    g_driveconfig_baseline = *cfg; /* new known-good baseline, for the Quit check */

    if (network_changed) {
        if (!save_network_to_firmware(&g_netconfig, msg)) {
            form_alert(1, msg);
            return 0; /* drives already saved; g_netconfig left untouched */
        }
        g_netconfig_baseline = g_netconfig; /* new known-good baseline */
    }

    if (rtc_changed) {
        if (!save_rtc_to_firmware(&g_rtcconfig, msg)) {
            form_alert(1, msg);
            return 0; /* drives/network already saved; g_rtcconfig left untouched */
        }
        g_rtcconfig_baseline = g_rtcconfig; /* new known-good baseline */
    }

    /* Fase 9B: one unified restart flow for anything that changed and
     * was successfully saved -- see configuration_changes_require_restart()
     * and offer_restart_if_needed(). Uses drives_changed/network_changed/
     * rtc_changed exactly as captured above, never re-derived against the
     * now-updated baselines. */
    restart_required = configuration_changes_require_restart(drives_changed, network_changed, rtc_changed);
    offer_restart_if_needed(restart_required);
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

    ov_dialog_init();
    ov_refresh_rows(cfg);

    form_center(ov_dlg, &x, &y, &w, &h);
    form_dial(FMD_START, x, y, w, h, x, y, w, h);
    objc_draw(ov_dlg, OV_ROOT, MAX_DEPTH, x, y, w, h);

    done = 0;
    while (!done) {
        which = (short)(form_do(ov_dlg, OV_ROOT) & 0x7FFF);
        wait_mouse_release();

        if (which == OV_SETTINGS_BTN) {
            cl_editor_run(cfg);
            ov_refresh_rows(cfg);
            objc_draw(ov_dlg, OV_ROOT, MAX_DEPTH, x, y, w, h);

        } else if (which >= OV_ROW_BASE && which < OV_AFTER_ROWS) {
            int obj_offset = which - OV_ROW_BASE;
            int slot       = obj_offset / 2;
            int is_btn     = (obj_offset % 2) == 1;

            if (is_btn) {
                Drive *d = &cfg->drives[slot];

                if (drive_slot_is_empty(d)) {
                    int type = add_disk_type_run();
                    if (type == DRIVE_TYPE_TNFS)
                        tnfs_editor_run(cfg, slot);
                    else if (type == DRIVE_TYPE_SD)
                        sd_editor_run(cfg, slot);
                    /* type == -1 (Cancel): slot stays EMPTY, nothing to do. */
                } else if (d->type == DRIVE_TYPE_TNFS) {
                    tnfs_editor_run(cfg, slot);
                } else {
                    sd_editor_run(cfg, slot);
                }
                /* Remove already clears the slot to EMPTY inside the
                 * editor itself -- nothing extra to do here. Slots are
                 * fixed-position: never re-sorted, never compacted. */
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
    int yt, ydiv1, ynet0, ynet1, ynet2, ydiv2,
        yclock0, yclock1, ydiv3, ylbldrv, ydrv0, ydiv4, ybtn;
    int i;

    graf_handle(&cw, &ch, &bw, &bh);
    wind_get(0, WF_WORKXYWH, &sx, &sy, &ssw, &sh);
    if (sh <= 0 || sh > 800) sh = 200;

    rh    = (sh >= 350) ? (int)ch : ((ch > 8) ? ch / 2 : (int)ch);
    tm    = (rh / 4 > 2) ? rh / 4 : 2;
    pitch = rh + 2; /* tighter than the editors' rh+3: read-only status text */

    DW = 44 * cw;
    xl = 2 * cw;

    /* Each section now starts straight with its own value lines: the
     * former "Network"/"Clock"/"Drives" headings are gone, because the
     * first line of each section carries the heading itself. */
    yt        = tm;
    ydiv1     = yt + rh + 1;
    ynet0     = ydiv1 + 5;
    ynet1     = ynet0 + pitch;
    ynet2     = ynet1 + pitch;
    ydiv2     = ynet2 + rh + 2;
    yclock0   = ydiv2 + 5;
    yclock1   = yclock0 + pitch;
    ydiv3     = yclock1 + rh + 2;
    ylbldrv   = ydiv3 + 5;
    ydrv0     = ylbldrv + pitch;
    /* +1 for the "(and N more)" line, which is always present as an
     * object and simply left blank when everything fits -- so the
     * dividers and buttons never move as the drive count changes. */
    ydiv4     = ydrv0 + (SW_NUM_DRIVE_LINES + 1) * pitch + 2;
    ybtn      = ydiv4 + 7;
    DH        = ybtn + rh + tm + 3;

    set_obj(sw_dlg, SW_ROOT, G_BOX, NONE, NORMAL, 0, 0, DW, DH);
    sw_dlg[SW_ROOT].ob_spec.index = 0x00031070L;

    set_obj(sw_dlg, SW_TITLE, G_STRING, NONE, NORMAL, 10*cw, yt, 24*cw, rh);
    sw_dlg[SW_TITLE].ob_spec.free_string = "SIDETNFS Configuration";

    set_obj(sw_dlg, SW_DIV1, G_BOX, NONE, NORMAL, cw, ydiv1, DW - 2*cw, 2);
    sw_dlg[SW_DIV1].ob_spec.index = 0x00001171L;

    set_obj(sw_dlg, SW_NET_LINE_0, G_STRING, NONE, NORMAL, xl, ynet0, SW_LINE_COLS*cw, rh);
    sw_dlg[SW_NET_LINE_0].ob_spec.free_string = sw_net_line[0];
    set_obj(sw_dlg, SW_NET_LINE_1, G_STRING, NONE, NORMAL, xl, ynet1, SW_LINE_COLS*cw, rh);
    sw_dlg[SW_NET_LINE_1].ob_spec.free_string = sw_net_line[1];
    set_obj(sw_dlg, SW_NET_LINE_2, G_STRING, NONE, NORMAL, xl, ynet2, SW_LINE_COLS*cw, rh);
    sw_dlg[SW_NET_LINE_2].ob_spec.free_string = sw_net_line[2];

    set_obj(sw_dlg, SW_DIV2, G_BOX, NONE, NORMAL, cw, ydiv2, DW - 2*cw, 2);
    sw_dlg[SW_DIV2].ob_spec.index = 0x00001171L;

    /* "Clock:" and the live date/time are two objects on one line: the
     * label never changes, so a tick repaints only the time. The time
     * starts in the shared value column, exactly like every other value
     * in this window, so it lines up with the rows above and below
     * instead of floating at its own coordinate. */
    set_obj(sw_dlg, SW_CLOCK_LBL, G_STRING, NONE, NORMAL, xl, yclock0, SW_LABEL_COLS*cw, rh);
    sw_dlg[SW_CLOCK_LBL].ob_spec.free_string = "Clock:";

    {
        int avail_chars = SW_LINE_COLS - SW_LABEL_COLS;

        if (avail_chars >= SW_CLOCK_FULL_CHARS)
            sw_clock_field_chars = SW_CLOCK_FULL_CHARS;
        else if (avail_chars >= SW_CLOCK_SHORT_CHARS)
            sw_clock_field_chars = SW_CLOCK_SHORT_CHARS;
        else
            sw_clock_field_chars = 0;

        sw_clock_now[0] = '\0';
        set_obj(sw_dlg, SW_CLOCK_NOW, G_STRING, NONE, NORMAL,
                xl + SW_LABEL_COLS*cw, yclock0, sw_clock_field_chars * cw, rh);
        sw_dlg[SW_CLOCK_NOW].ob_spec.free_string = sw_clock_now;
    }

    set_obj(sw_dlg, SW_CLOCK_NTP, G_STRING, NONE, NORMAL, xl, yclock1, SW_LINE_COLS*cw, rh);
    sw_dlg[SW_CLOCK_NTP].ob_spec.free_string = sw_clock_ntp;

    set_obj(sw_dlg, SW_DIV3, G_BOX, NONE, NORMAL, cw, ydiv3, DW - 2*cw, 2);
    sw_dlg[SW_DIV3].ob_spec.index = 0x00001171L;

    set_obj(sw_dlg, SW_DRIVE_HEADER, G_STRING, NONE, NORMAL, xl, ylbldrv, SW_LINE_COLS*cw, rh);
    sw_dlg[SW_DRIVE_HEADER].ob_spec.free_string = sw_drive_header;

    for (i = 0; i < SW_NUM_DRIVE_LINES; i++) {
        int ry = ydrv0 + i * pitch;
        set_obj(sw_dlg, SW_DRIVE_LINE(i), G_STRING, NONE, NORMAL, xl, ry, SW_LINE_COLS*cw, rh);
        sw_dlg[SW_DRIVE_LINE(i)].ob_spec.free_string = sw_drive_line[i];
    }

    set_obj(sw_dlg, SW_DRIVE_MORE, G_STRING, NONE, NORMAL, xl,
            ydrv0 + SW_NUM_DRIVE_LINES * pitch, SW_LINE_COLS*cw, rh);
    sw_dlg[SW_DRIVE_MORE].ob_spec.free_string = sw_drive_more;

    set_obj(sw_dlg, SW_DIV4, G_BOX, NONE, NORMAL, cw, ydiv4, DW - 2*cw, 2);
    sw_dlg[SW_DIV4].ob_spec.index = 0x00001171L;

    set_obj(sw_dlg, SW_CONFIG, G_BUTTON, EXIT | TOUCHEXIT, NORMAL, 2*cw, ybtn, 9*cw, rh);
    sw_dlg[SW_CONFIG].ob_spec.free_string = " Config  ";

    set_obj(sw_dlg, SW_DRIVES, G_BUTTON, EXIT | TOUCHEXIT, NORMAL, 12*cw, ybtn, 9*cw, rh);
    sw_dlg[SW_DRIVES].ob_spec.free_string = " Drives  ";

    set_obj(sw_dlg, SW_SAVE, G_BUTTON, EXIT | DEFAULT | TOUCHEXIT, NORMAL, 22*cw, ybtn, 9*cw, rh);
    sw_dlg[SW_SAVE].ob_spec.free_string = "  Save   ";

    set_obj(sw_dlg, SW_QUIT, G_BUTTON, EXIT | TOUCHEXIT, NORMAL, 32*cw, ybtn, 9*cw, rh);
    sw_dlg[SW_QUIT].ob_spec.free_string = "  Quit   ";

    wire_tree(sw_dlg, SW_NOBJS);

    (void)sx; (void)sy; (void)ssw; (void)bw; (void)bh;
}

/* Left-justifies label in an SW_LABEL_COLS field, so every value in the
 * window starts in the same column whichever label it follows. The value
 * is bounded to what is left of the line, so a long value can never run
 * past the object and over its neighbours. */
static void sw_set_labelled_line(char *dst, const char *label, const char *value)
{
    sprintf(dst, "%-*s%.*s", SW_LABEL_COLS, label, SW_LINE_COLS - SW_LABEL_COLS, value);
    dst[SW_LINE_BUF - 1] = '\0';
}

/* Renders the IP line's value into `out` (at least 32 bytes).
 *
 * In DHCP mode the address the Pico actually got is NOT available to this
 * program: the firmware keeps it in network.c's `current_ip` and only ever
 * prints it to the debug UART. GET_NETWORK_CONFIG returns the STORED
 * configuration (sys.ip_address), which in DHCP mode is the unused
 * static-IP field -- passing that off as the active address would be a
 * plain lie. So DHCP says who assigns the address instead of pretending
 * to know it.
 *
 * In static mode the stored address is the one the Pico uses, so it is
 * shown as-is. The Network line above already reports whether the link is
 * actually up, so this line does not need to hedge about that too. */
static void sw_format_ip_value(const NetConfig *net, char *out)
{
    if (net->ip_mode == NETCONFIG_MODE_DHCP)
        strcpy(out, "Set by DHCP server");
    else if (net->ip_address[0] != '\0')
        sprintf(out, "%.28s", net->ip_address);
    else
        strcpy(out, "-");
}

/* Reads only g_netconfig (the one-time network_startup_load() result,
 * kept current afterwards by the CONFIG editor) plus the live link
 * status long -- never calls sidetnfs_probe_get_network_config() itself.
 * GET_NETWORK_CONFIG/0x0413 is sent exactly once per session, in
 * network_startup_load(). */
static void status_refresh_network(const DriveConfig *cfg)
{
    char value[40];
    int link_connected;

    (void)cfg; /* TNFS servers belong to the drive list, not to network status */

    link_connected = (sidetnfs_probe_get_network_link_state() == SIDETNFS_NET_LINK_CONNECTED);

    /* The link state comes from GEMDRVEMUL_NETWORK_STATUS, the same
     * status long the GEMDRIVE ROM waits on during boot, so "Connected"
     * is real evidence that WiFi came up -- never inferred from the
     * stored settings. GET_NETWORK_CONFIG (0x0413) only proves the saved
     * configuration could be read, which is why its own outcome is
     * reported first: without a firmware answer at all there is nothing
     * to say about the link either. */
    switch (g_netconfig_load_state) {
    case NETLOAD_OK:
        if (link_connected)
            strncpy(value, "Connected", sizeof(value) - 1);
        else if (g_netconfig.ssid[0] == '\0')
            strncpy(value, "Not configured", sizeof(value) - 1);
        else
            strncpy(value, "Not connected", sizeof(value) - 1);
        break;
    case NETLOAD_BAD_STATUS:
        strncpy(value, netconfig_status_text(g_netconfig_load_fw_status), sizeof(value) - 1);
        break;
    case NETLOAD_UNAVAILABLE:
    default:
        strncpy(value, "Unavailable", sizeof(value) - 1);
        break;
    }
    value[sizeof(value) - 1] = '\0';
    sw_set_labelled_line(sw_net_line[0], "Network:", value);

    /* SSID only -- the password is never shown here. */
    sw_set_labelled_line(sw_net_line[1], "SSID:",
                         (g_netconfig.ssid[0] != '\0') ? g_netconfig.ssid : "-");

    sw_format_ip_value(&g_netconfig, value);
    sw_set_labelled_line(sw_net_line[2], "IP:", value);
}

/* GEMDOS Tgetdate(): bits 0-4 day (1-31), bits 5-8 month (1-12),
 * bits 9-15 year counted from 1980. */
static void gemdos_decode_date(unsigned short packed, int *year, int *month, int *day)
{
    *day   = (int)(packed & 0x1FU);
    *month = (int)((packed >> 5) & 0x0FU);
    *year  = 1980 + (int)((packed >> 9) & 0x7FU);
}

/* GEMDOS Tgettime(): bits 0-4 seconds in TWO-second steps, bits 5-10
 * minutes (0-59), bits 11-15 hours (0-23). The doubling is why the
 * seconds field only ever shows even values. */
static void gemdos_decode_time(unsigned short packed, int *hour, int *minute, int *second)
{
    *second = (int)((packed & 0x1FU) * 2U);
    *minute = (int)((packed >> 5) & 0x3FU);
    *hour   = (int)((packed >> 11) & 0x1FU);
}

/* Renders the Atari clock into `out` (at least SW_CLOCK_TEXT_BUF bytes).
 * `field_chars` is what the laid-out field can actually hold, so a field
 * too narrow for the full form falls back to time only rather than being
 * clipped mid-string. Always writes exactly field_chars characters when
 * it writes anything, so a shrinking value can never leave part of the
 * previous one behind. */
static void gemdos_format_clock(unsigned short date, unsigned short time, int field_chars, char *out)
{
    int year, month, day, hour, minute, second;

    if (field_chars >= SW_CLOCK_FULL_CHARS) {
        gemdos_decode_date(date, &year, &month, &day);
        gemdos_decode_time(time, &hour, &minute, &second);
        sprintf(out, "%02d-%02d-%04d %02d:%02d:%02d", day, month, year, hour, minute, second);
    } else if (field_chars >= SW_CLOCK_SHORT_CHARS) {
        gemdos_decode_time(time, &hour, &minute, &second);
        sprintf(out, "%02d:%02d:%02d", hour, minute, second);
    } else {
        out[0] = '\0';
    }
}

/* Reads the Atari's own system clock -- never the Pico's. What this shows
 * is the time the Atari actually has, which is the point: it is how the
 * user sees whether the boot-time clock hand-off worked. */
static void status_refresh_clock_text(void)
{
    gemdos_format_clock((unsigned short)Tgetdate(), (unsigned short)Tgettime(),
                        sw_clock_field_chars, sw_clock_now);
}

/* Re-renders the clock and reports whether the text actually changed, so
 * a tick that lands inside the same GEMDOS two-second step redraws
 * nothing at all. */
static int status_clock_tick(void)
{
    char previous[SW_CLOCK_TEXT_BUF];

    strcpy(previous, sw_clock_now);
    status_refresh_clock_text();
    return strcmp(previous, sw_clock_now) != 0;
}

/* Never inferred: SIDETNFS_RTC_SYNC_SYNCED is only ever returned when the
 * Pico explicitly published a synchronised clock this boot. */
static const char *rtc_sync_text(int state)
{
    switch (state) {
    case SIDETNFS_RTC_SYNC_SYNCED:   return "Synchronized";
    case SIDETNFS_RTC_SYNC_DISABLED: return "Disabled";
    case SIDETNFS_RTC_SYNC_NOT_SYNCED:
    default:                          return "Not synchronized";
    }
}

/* Renders the stored whole-hour UTC offset ("0", "+1", "-5") as
 * "UTC+00:00" / "UTC+01:00" / "UTC-05:00". Returns 0 for anything the
 * firmware would not have accepted, so the caller can fall back to "-"
 * rather than show a made-up zone. Whole hours are all the firmware
 * stores and all it applies -- there is no zone name and no DST here to
 * render. */
static int format_utc_offset(const char *offset, char *out)
{
    const char *p = offset;
    int sign = 1;
    int value = 0;
    int digits = 0;

    if (p == 0 || *p == '\0')
        return 0;

    if (*p == '+') {
        p++;
    } else if (*p == '-') {
        sign = -1;
        p++;
    }

    while (*p >= '0' && *p <= '9') {
        value = value * 10 + (*p - '0');
        p++;
        digits++;
        if (digits > 2)
            return 0;
    }
    if (digits == 0 || *p != '\0')
        return 0;

    /* Same range the firmware's own parse_utc_offset() accepts. */
    if (sign > 0 ? (value > 14) : (value > 12))
        return 0;

    sprintf(out, "UTC%c%02d:00", (sign < 0) ? '-' : '+', value);
    return 1;
}

/* NTP comes from the live status the Pico publishes; Timezone comes from
 * the stored RTC configuration g_rtcconfig already holds (GET_RTC_CONFIG,
 * sent once in rtc_startup_load()). No new command is sent from here. */
static void status_refresh_clock(void)
{
    char tz[32];
    char value[SW_LINE_BUF];

    if (!format_utc_offset(g_rtcconfig.utc_offset, tz))
        strcpy(tz, "-");

    /* Both on one line. The longest form,
     * "Not synchronized  TZ: UTC+01:00", is 31 characters, which is what
     * the SW_LINE_COLS - SW_LABEL_COLS budget below has to hold. */
    sprintf(value, "%-16s  TZ: %s", rtc_sync_text(sidetnfs_probe_get_rtc_sync_state()), tz);
    sw_set_labelled_line(sw_clock_ntp, "NTP:", value);

    status_refresh_clock_text();
}

/* One row of the active-drive list, laid out across the full
 * SW_LINE_COLS: letter, then the name, then the backend flush right.
 *
 *   col 0..1   "N:"
 *   col 2..3   two spaces
 *   col 4..    name, truncated to SW_DRV_NAME_COLS
 *   last 8     backend, right-aligned
 *
 * The name field is a fixed width with a precision, so an over-long name
 * is cut rather than pushing the backend out of view, and a short one is
 * padded so the backend always lands in the same column. */
#define SW_DRV_NAME_COL     4
#define SW_DRV_BACKEND_COLS 8 /* "SETTINGS", the longest backend name */
#define SW_DRV_NAME_COLS    (SW_LINE_COLS - SW_DRV_NAME_COL - SW_DRV_BACKEND_COLS - 1)

static void sw_format_drive_row(char *dst, char letter, const char *name, const char *backend)
{
    if (name == 0 || name[0] == '\0')
        name = "(unnamed)";

    sprintf(dst, "%c:  %-*.*s %*s", letter,
            SW_DRV_NAME_COLS, SW_DRV_NAME_COLS, name,
            SW_DRV_BACKEND_COLS, backend);
    dst[SW_LINE_BUF - 1] = '\0';
}

/* Collects every drive that is actually published as a GEMDOS drive: the
 * ENABLED ordinary slots plus the SETTINGS disk, which is always active.
 * A DISABLED slot is stored but never published, so it is not listed.
 * Slots keep their fixed indices and are never sorted in the model, so
 * the ascending order is produced here, by drive letter, without
 * reordering anything the rest of the program relies on.
 *
 * Returns the number collected; fills up to `max` entries. A TNFS drive
 * currently serving NET_ERR.TXT is published like any other and is
 * therefore listed like any other -- its backend does not change. */
typedef struct {
    char letter;
    const char *name;
    const char *backend;
} SwDriveRow;

static int sw_collect_active_drives(const DriveConfig *cfg, SwDriveRow *out, int max)
{
    int i, j, count = 0;
    SwDriveRow all[MAX_DRIVES + 1];

    for (i = 0; i < MAX_DRIVES; i++) {
        const Drive *d = &cfg->drives[i];
        if (!drive_slot_is_enabled(d))
            continue;
        all[count].letter  = d->letter;
        all[count].name    = d->nickname;
        all[count].backend = (d->type == DRIVE_TYPE_TNFS) ? "TNFS" : "SD";
        count++;
    }

    /* The SETTINGS disk is an ordinary row in this list, not a line of
     * its own any more. */
    all[count].letter  = cfg->settings_letter;
    all[count].name    = "Settings";
    all[count].backend = "SETTINGS";
    count++;

    /* Insertion sort by letter: at most nine entries, and it keeps the
     * caller free of any assumption that the letters are contiguous. */
    for (i = 1; i < count; i++) {
        SwDriveRow key = all[i];
        for (j = i - 1; j >= 0 && all[j].letter > key.letter; j--)
            all[j + 1] = all[j];
        all[j + 1] = key;
    }

    for (i = 0; i < count && i < max; i++)
        out[i] = all[i];

    return count;
}

/* Header carries the total number of published drives, right-aligned on
 * the same line; the rows below show at most SW_NUM_DRIVE_LINES of them,
 * with the remainder acknowledged rather than silently dropped. */
static void status_refresh_drives(const DriveConfig *cfg)
{
    SwDriveRow rows[SW_NUM_DRIVE_LINES];
    int total, shown, i;
    char count_text[8];

    total = sw_collect_active_drives(cfg, rows, SW_NUM_DRIVE_LINES);
    shown = (total < SW_NUM_DRIVE_LINES) ? total : SW_NUM_DRIVE_LINES;

    sprintf(count_text, "%d", total);
    sprintf(sw_drive_header, "%-*s%*s",
            SW_LINE_COLS - (int)strlen(count_text), "Active drives:",
            (int)strlen(count_text), count_text);
    sw_drive_header[SW_LINE_BUF - 1] = '\0';

    for (i = 0; i < shown; i++)
        sw_format_drive_row(sw_drive_line[i], rows[i].letter, rows[i].name, rows[i].backend);
    for (; i < SW_NUM_DRIVE_LINES; i++)
        sw_drive_line[i][0] = '\0';

    if (total > shown)
        sprintf(sw_drive_more, "(and %d more)", total - shown);
    else
        sw_drive_more[0] = '\0';
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

/* Repaints only the clock field: the root is drawn with the clip
 * rectangle narrowed to that one object, so the AES repaints its
 * background and the string within it and touches nothing else. That
 * keeps the update flicker-free and, because the background is repainted
 * too, guarantees no part of the previous time can survive. */
static void sw_redraw_clock(void)
{
    short ox, oy;

    if (sw_clock_field_chars <= 0)
        return;

    objc_offset(sw_dlg, SW_CLOCK_NOW, &ox, &oy);
    objc_draw(sw_dlg, SW_ROOT, MAX_DEPTH, ox, oy,
              sw_dlg[SW_CLOCK_NOW].ob_width, sw_dlg[SW_CLOCK_NOW].ob_height);
}

/* form_do() has no timer hook, so the status window runs the equivalent
 * evnt_multi() loop itself. Behaviour matches form_do() for this window:
 * TOUCHEXIT buttons fire on mouse-down and Return activates the DEFAULT
 * button. It is a plain form_dial()-style modal with no editable fields,
 * so none of form_do()'s text-cursor handling applies here.
 *
 * MU_TIMER only refreshes the clock. The loop always returns to
 * evnt_multi() immediately afterwards, so the AES event loop is never
 * blocked and redraw/clipping stays the AES's own business. */
static short sw_form_do_ticking(void)
{
    short mx, my, mb, ks, kr, br;
    short msg[8];
    short event, obj, next;
    short result = SW_QUIT;
    int running = 1;

    while (running) {
        /* gemlib takes the timer interval as a single unsigned long, and
         * returns OutX/OutY/ButtonState/KeyState/Key/ReturnCount in that
         * order -- `kr` is the key code, `br` the click count. */
        event = evnt_multi(MU_KEYBD | MU_BUTTON | MU_TIMER | MU_MESAG,
                           2, 1, 1,
                           0, 0, 0, 0, 0,
                           0, 0, 0, 0, 0,
                           msg,
                           (unsigned long)SW_CLOCK_TICK_MS,
                           &mx, &my, &mb, &ks, &kr, &br);

        if (event & MU_TIMER) {
            if (status_clock_tick())
                sw_redraw_clock();
        }

        if (event & MU_BUTTON) {
            obj = objc_find(sw_dlg, SW_ROOT, MAX_DEPTH, mx, my);
            if (obj > 0) {
                next = obj;
                if (!form_button(sw_dlg, obj, br, &next)) {
                    result = (short)(next & 0x7FFF);
                    running = 0;
                    /* Leave the button unpressed, so the full redraw the
                     * caller does after the action does not show it stuck
                     * in its selected state. */
                    if (result > 0 && result < SW_NOBJS)
                        sw_dlg[result].ob_state &= (unsigned short)(~SELECTED);
                }
            }
        }

        if (event & MU_KEYBD) {
            /* No editable fields here, so the only key that does
             * anything is Return/Enter on the DEFAULT button. */
            if ((kr & 0x00FF) == 0x0D) {
                result = SW_SAVE;
                running = 0;
            }
        }
    }

    return result;
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
    g_driveconfig_baseline = *cfg; /* known-good snapshot, for the Quit-with-unsaved-changes check below */

    /* Fase AC-4 (network protocol): loaded after the drive protocol,
     * regardless of firmware_backed -- with no firmware at all this
     * simply times out too and falls back the same way. */
    network_startup_load();

    /* Fase 12A (RTC/NTP protocol): loaded after network config, same
     * fallback shape -- unsupported (older) firmware or no firmware at
     * all both just leave g_rtcconfig at its built-in defaults. */
    rtc_startup_load();

    sw_dialog_init();
    status_window_refresh(cfg);

    form_center(sw_dlg, &x, &y, &w, &h);
    form_dial(FMD_START, x, y, w, h, x, y, w, h);
    objc_draw(sw_dlg, SW_ROOT, MAX_DEPTH, x, y, w, h);

    done = 0;
    while (!done) {
        which = sw_form_do_ticking();
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

        case SW_QUIT: {
            /* Unsaved-changes warning: drives compared against the
             * last-loaded/last-saved snapshot (g_driveconfig_baseline),
             * network/RTC against their own existing baselines -- same
             * "only if actually changed" idiom perform_save() already
             * uses to decide what to write. */
            int drives_changed = !drive_config_matches(cfg, &g_driveconfig_baseline);
            int net_changed    = netconfig_changed(&g_netconfig, &g_netconfig_baseline);
            int rtc_pending    = rtcconfig_changed(&g_rtcconfig, &g_rtcconfig_baseline);

            if (drives_changed || net_changed || rtc_pending) {
                /* Cancel is the default (button 1): quitting with
                 * unsaved changes is easy to trigger by accident
                 * otherwise. */
                if (form_alert(1, "[2][Unsaved changes.|Quit without saving?][Cancel|Quit Anyway]") == 2)
                    done = 1;
            } else {
                done = 1;
            }
            break;
        }

        default:
            done = 1;
            break;
        }
    }

    form_dial(FMD_FINISH, x, y, w, h, x, y, w, h);
}
