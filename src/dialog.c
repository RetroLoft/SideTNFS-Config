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
#define OV_SAVE   (OV_AFTER_ROWS + 2)
#define OV_CANCEL (OV_AFTER_ROWS + 3)
#define OV_NOBJS  (OV_AFTER_ROWS + 4)

static OBJECT cl_dlg[CL_NOBJS];
static OBJECT sd_dlg[SD_NOBJS];
static OBJECT te_dlg[TE_NOBJS];
static OBJECT ov_dlg[OV_NOBJS];

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
/* Drive-overview row text buffers                                     */
/* ================================================================== */
#define OV_ROW_BUF 40
static char ov_row_text[MAX_DRIVES][OV_ROW_BUF];

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
    ynick = ydiv1 + 2;
    ytype = ynick + pitch;
    ydrv  = ytype + pitch;
    ydiv2 = ydrv + rh + 2;
    ybtn  = ydiv2 + 4;
    DH    = ybtn + rh + tm;

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
    ydrv  = ydiv1 + 2;
    ynick = ydrv + pitch;
    ypath = ynick + pitch;
    ydiv2 = ypath + rh + 2;
    ybtn  = ydiv2 + 4;
    DH    = ybtn + rh + tm;

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
    ydrv       = ydiv1 + 2;
    ynick      = ydrv + pitch;
    yhost      = ynick + pitch;
    yport      = yhost + pitch;
    ytrans     = yport + pitch;
    ymount     = ytrans + pitch;
    ymounthint = ymount + pitch;
    ydiv2      = ymounthint + rh + 2;
    ybtn       = ydiv2 + 4;
    DH         = ybtn + rh + tm;

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
    ybtn  = ydiv1 + 4;
    DH    = ybtn + rh + tm;

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
    yrow0 = ydiv1 + 2;
    ydiv2 = yrow0 + MAX_DRIVES * row_pitch + 2;
    ybtn  = ydiv2 + 4;
    DH    = ybtn + rh + tm;

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

    set_obj(ov_dlg, OV_SAVE, G_BUTTON, EXIT | DEFAULT | TOUCHEXIT, NORMAL, 18*cw, ybtn, 8*cw, rh);
    ov_dlg[OV_SAVE].ob_spec.free_string = "  Save  ";

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

/* ================================================================== */
/* Main entry point                                                    */
/* ================================================================== */

void dialog_run(DriveConfig *cfg)
{
    short x, y, w, h;
    short which;
    int done;
    int i;
    int firmware_backed;

    shared_fields_init();

    /* No local file storage: *cfg starts out as the built-in defaults
     * (drive_config_init_defaults(), set by main.c) and is fully
     * replaced by the firmware's own drive list when one is found. See
     * dialog_startup_load(). Without firmware, Save has nothing to
     * write to and says so. */
    firmware_backed = dialog_startup_load(cfg);

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

        } else if (which == OV_SAVE) {
            int all_valid = 1;
            char msg[220];

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

            if (all_valid && firmware_backed) {
                /* The full write sequence + readback verification lives
                 * in save_to_firmware(); this is now the only place
                 * config is ever persisted -- no local file involved. */
                if (save_to_firmware(cfg, msg)) {
                    /* Cancel is the default (button 1): a full-machine
                     * reset is easy to trigger by accident otherwise. */
                    if (form_alert(1, "[1][Configuration saved to SideTNFS.|Reboot required.|"
                                      "Multi-drive mounting is not active yet.][Cancel|Reset Now]") == 2) {
                        Supexec(atari_do_reset);
                    }
                    done = 1;
                } else {
                    form_alert(1, msg);
                }
            } else if (all_valid) {
                /* No firmware detected -- there is nowhere left to save
                 * to (no local file fallback anymore). */
                form_alert(1, "[3][Save failed|No SideTNFS firmware detected.|Nothing was saved.][OK]");
            }

        } else if (which == OV_CANCEL) {
            done = 1;
        }
    }

    form_dial(FMD_FINISH, x, y, w, h, x, y, w, h);
}
