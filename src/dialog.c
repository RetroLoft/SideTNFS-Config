/*
 * SideTNFS Configuration - GEM dialogs built in C, no .RSC file.
 *
 * Target : Atari ST / Mega STE, TOS 1.x / 2.x, 68000 CPU.
 * Minimum: 640x200 medium resolution.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dialog.h"
#include "profile.h"
#include "config.h"
#include "sidetnfs_probe.h"

/* ================================================================== */
/* Main dialog object indices                                          */
/* ================================================================== */
enum {
    OBJ_ROOT = 0,
    OBJ_TITLE,
    OBJ_DIV1,
    OBJ_LBL_PROFILE,
    OBJ_LBL_ACTIVE,    OBJ_ACTIVE_EDIT,   OBJ_LIST_BTN,
    OBJ_LBL_NAME,      OBJ_NAME_EDIT,
    OBJ_LBL_SERVER,    OBJ_SERVER_EDIT,
    OBJ_LBL_PORT,      OBJ_PORT_EDIT,
    OBJ_LBL_PROTOCOL,  OBJ_PROTOCOL_EDIT,
    OBJ_LBL_MOUNT,     OBJ_MOUNT_EDIT,
    OBJ_READONLY,
    OBJ_LBL_DRIVES,
    OBJ_LBL_TNFS,      OBJ_TNFS_DRIVE,    OBJ_LBL_TNFS_COL,
    OBJ_LBL_CFG,       OBJ_CONFIG_DRIVE,  OBJ_LBL_CFG_COL,
    OBJ_LBL_STATUS,    OBJ_STATUS_TEXT,
    OBJ_DIV2,
    OBJ_TEST,          OBJ_SAVE,          OBJ_CANCEL,
    N_OBJS   /* = 31 */
};

/* ================================================================== */
/* Profile list dialog object indices                                  */
/* ================================================================== */
enum {
    PL_ROOT = 0,
    PL_TITLE,
    PL_DIV1,
    PL_ROW_0, PL_ROW_1, PL_ROW_2, PL_ROW_3,
    PL_ROW_4, PL_ROW_5, PL_ROW_6, PL_ROW_7,
    PL_DIV2,
    PL_USE, PL_NEW, PL_DELETE, PL_CANCEL,
    PL_NOBJS  /* = 16 */
};

/* ================================================================== */
/* Main dialog text buffers and TEDINFOs                              */
/* ================================================================== */
#define BUF_LONG  32
#define BUF_PORT   7
#define BUF_DRV    3

static char buf_active  [BUF_LONG];
static char buf_name    [BUF_LONG];
static char buf_server  [BUF_LONG];
static char buf_port    [BUF_PORT];
static char buf_protocol[BUF_LONG];
static char buf_mount   [BUF_LONG];
static char buf_tnfs    [BUF_DRV];
static char buf_cfg     [BUF_DRV];

static char tmpl_long[BUF_LONG];
static char vld_long [BUF_LONG];
static char tmpl_port[BUF_PORT];
static char vld_port [BUF_PORT];
static char tmpl_drv [BUF_DRV];
static char vld_drv  [BUF_DRV];

static TEDINFO ti_active, ti_name, ti_server, ti_port,
               ti_protocol, ti_mount, ti_tnfs, ti_cfg;

/* ================================================================== */
/* Object trees                                                        */
/* ================================================================== */
static OBJECT dlg   [N_OBJS];
static OBJECT pl_dlg[PL_NOBJS];

/* ================================================================== */
/* Profile list row text buffers (2-char prefix + name + NUL)         */
/* ================================================================== */
#define PL_ROW_BUF 36
static char pl_row_text[MAX_PROFILES][PL_ROW_BUF];

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

/* ================================================================== */
/* Main dialog — object tree build                                     */
/* ================================================================== */

static void dialog_init(void)
{
    short cw, ch, bw, bh;
    short sx, sy, sw, sh;
    int rh, tm, pitch, sect;
    int DW, DH, xl, xf;
    int yt, ydiv1, yprf, yact, ynam, ysvr, yprt, yproto, ymnt;
    int yrdonly, ydrv, ydrvr, ystat, ystatx, ydiv2, ybtn;

    graf_handle(&cw, &ch, &bw, &bh);
    wind_get(0, WF_WORKXYWH, &sx, &sy, &sw, &sh);
    if (sh <= 0 || sh > 800) sh = 200;

    rh    = (sh >= 350) ? (int)ch : ((ch > 8) ? ch / 2 : (int)ch);
    tm    = (rh / 4 > 2) ? rh / 4 : 2;
    pitch = rh + 3;
    sect  = rh + 5;

    DW = 50 * cw;
    xl = 2 * cw;
    xf = 13 * cw;

    yt      = tm;
    ydiv1   = yt      + rh + 1;
    yprf    = ydiv1   + 2;
    yact    = yprf    + pitch;
    ynam    = yact    + pitch;
    ysvr    = ynam    + pitch;
    yprt    = ysvr    + pitch;
    yproto  = yprt    + pitch;
    ymnt    = yproto  + pitch;
    yrdonly = ymnt    + sect;
    ydrv    = yrdonly + sect;
    ydrvr   = ydrv    + pitch;
    ystat   = ydrvr   + sect;
    ystatx  = ystat   + pitch;
    ydiv2   = ystatx  + rh + 2;
    ybtn    = ydiv2   + 2  + 2;
    DH      = ybtn    + rh + tm;

    /* --- templates --- */
    fill_n(tmpl_long, '_', BUF_LONG - 1);
    fill_n(vld_long,  'X', BUF_LONG - 1);
    fill_n(tmpl_port, '_', BUF_PORT - 1);
    fill_n(vld_port,  '9', BUF_PORT - 1);
    fill_n(tmpl_drv,  '_', BUF_DRV  - 1);
    fill_n(vld_drv,   'X', BUF_DRV  - 1);

    /* --- TEDINFOs (buffers filled later by dialog_load_from_config) --- */
    init_ti(&ti_active,   buf_active,   tmpl_long, vld_long, BUF_LONG);
    init_ti(&ti_name,     buf_name,     tmpl_long, vld_long, BUF_LONG);
    init_ti(&ti_server,   buf_server,   tmpl_long, vld_long, BUF_LONG);
    init_ti(&ti_port,     buf_port,     tmpl_port, vld_port, BUF_PORT);
    init_ti(&ti_protocol, buf_protocol, tmpl_long, vld_long, BUF_LONG);
    init_ti(&ti_mount,    buf_mount,    tmpl_long, vld_long, BUF_LONG);
    init_ti(&ti_tnfs,     buf_tnfs,     tmpl_drv,  vld_drv,  BUF_DRV);
    init_ti(&ti_cfg,      buf_cfg,      tmpl_drv,  vld_drv,  BUF_DRV);

    /* --- root --- */
    set_obj(dlg, OBJ_ROOT, G_BOX, NONE, NORMAL, 0, 0, DW, DH);
    dlg[OBJ_ROOT].ob_spec.index = 0x00031070L;

    /* --- title / top divider --- */
    set_obj(dlg, OBJ_TITLE, G_STRING, NONE, NORMAL,
            14*cw, yt, 22*cw, rh);
    dlg[OBJ_TITLE].ob_spec.free_string = "SideTNFS Configuration";

    set_obj(dlg, OBJ_DIV1, G_BOX, NONE, NORMAL,
            cw, ydiv1, DW - 2*cw, 2);
    dlg[OBJ_DIV1].ob_spec.index = 0x00001171L;

    /* --- "Profile" label --- */
    set_obj(dlg, OBJ_LBL_PROFILE, G_STRING, NONE, NORMAL,
            xl, yprf, 7*cw, rh);
    dlg[OBJ_LBL_PROFILE].ob_spec.free_string = "Profile";

    /* --- Active row --- */
    set_obj(dlg, OBJ_LBL_ACTIVE, G_STRING, NONE, NORMAL,
            xl, yact, 11*cw, rh);
    dlg[OBJ_LBL_ACTIVE].ob_spec.free_string = "Active:";

    set_obj(dlg, OBJ_ACTIVE_EDIT, G_FBOXTEXT, EDITABLE, NORMAL,
            xf, yact, 24*cw, rh);
    dlg[OBJ_ACTIVE_EDIT].ob_spec.tedinfo = &ti_active;

    /* TOUCHEXIT: bypasses TOS 2.06 bug where EXIT buttons are ignored
     * while a text EDITABLE field is active in form_do(). */
    set_obj(dlg, OBJ_LIST_BTN, G_BUTTON, EXIT | TOUCHEXIT, NORMAL,
            xf + 25*cw, yact, 8*cw, rh);
    dlg[OBJ_LIST_BTN].ob_spec.free_string = " List ";

    /* --- Name row --- */
    set_obj(dlg, OBJ_LBL_NAME, G_STRING, NONE, NORMAL,
            xl, ynam, 11*cw, rh);
    dlg[OBJ_LBL_NAME].ob_spec.free_string = "Name:";

    set_obj(dlg, OBJ_NAME_EDIT, G_FBOXTEXT, EDITABLE, NORMAL,
            xf, ynam, 31*cw, rh);
    dlg[OBJ_NAME_EDIT].ob_spec.tedinfo = &ti_name;

    /* --- Server row --- */
    set_obj(dlg, OBJ_LBL_SERVER, G_STRING, NONE, NORMAL,
            xl, ysvr, 11*cw, rh);
    dlg[OBJ_LBL_SERVER].ob_spec.free_string = "Server:";

    set_obj(dlg, OBJ_SERVER_EDIT, G_FBOXTEXT, EDITABLE, NORMAL,
            xf, ysvr, 31*cw, rh);
    dlg[OBJ_SERVER_EDIT].ob_spec.tedinfo = &ti_server;

    /* --- Port row --- */
    set_obj(dlg, OBJ_LBL_PORT, G_STRING, NONE, NORMAL,
            xl, yprt, 11*cw, rh);
    dlg[OBJ_LBL_PORT].ob_spec.free_string = "Port:";

    set_obj(dlg, OBJ_PORT_EDIT, G_FBOXTEXT, EDITABLE, NORMAL,
            xf, yprt, 6*cw, rh);
    dlg[OBJ_PORT_EDIT].ob_spec.tedinfo = &ti_port;

    /* --- Protocol row --- */
    set_obj(dlg, OBJ_LBL_PROTOCOL, G_STRING, NONE, NORMAL,
            xl, yproto, 11*cw, rh);
    dlg[OBJ_LBL_PROTOCOL].ob_spec.free_string = "Protocol:";

    set_obj(dlg, OBJ_PROTOCOL_EDIT, G_FBOXTEXT, EDITABLE, NORMAL,
            xf, yproto, 10*cw, rh);
    dlg[OBJ_PROTOCOL_EDIT].ob_spec.tedinfo = &ti_protocol;

    /* --- Mount dir row --- */
    set_obj(dlg, OBJ_LBL_MOUNT, G_STRING, NONE, NORMAL,
            xl, ymnt, 11*cw, rh);
    dlg[OBJ_LBL_MOUNT].ob_spec.free_string = "Mount dir:";

    set_obj(dlg, OBJ_MOUNT_EDIT, G_FBOXTEXT, EDITABLE, NORMAL,
            xf, ymnt, 31*cw, rh);
    dlg[OBJ_MOUNT_EDIT].ob_spec.tedinfo = &ti_mount;

    /* --- Read only toggle (SELECTABLE: form_do toggles, does not return) --- */
    set_obj(dlg, OBJ_READONLY, G_BUTTON, SELECTABLE, NORMAL,
            xl, yrdonly, 14*cw, rh);
    dlg[OBJ_READONLY].ob_spec.free_string = "  Read only";

    /* --- Drives section --- */
    set_obj(dlg, OBJ_LBL_DRIVES, G_STRING, NONE, NORMAL,
            xl, ydrv, 6*cw, rh);
    dlg[OBJ_LBL_DRIVES].ob_spec.free_string = "Drives";

    set_obj(dlg, OBJ_LBL_TNFS, G_STRING, NONE, NORMAL,
            xl, ydrvr, 10*cw, rh);
    dlg[OBJ_LBL_TNFS].ob_spec.free_string = "TNFS disk:";

    set_obj(dlg, OBJ_TNFS_DRIVE, G_FBOXTEXT, EDITABLE, NORMAL,
            xl + 11*cw, ydrvr, 2*cw, rh);
    dlg[OBJ_TNFS_DRIVE].ob_spec.tedinfo = &ti_tnfs;

    set_obj(dlg, OBJ_LBL_TNFS_COL, G_STRING, NONE, NORMAL,
            xl + 14*cw, ydrvr, cw, rh);
    dlg[OBJ_LBL_TNFS_COL].ob_spec.free_string = ":";

    set_obj(dlg, OBJ_LBL_CFG, G_STRING, NONE, NORMAL,
            xl + 17*cw, ydrvr, 12*cw, rh);
    dlg[OBJ_LBL_CFG].ob_spec.free_string = "Config disk:";

    set_obj(dlg, OBJ_CONFIG_DRIVE, G_FBOXTEXT, EDITABLE, NORMAL,
            xl + 30*cw, ydrvr, 2*cw, rh);
    dlg[OBJ_CONFIG_DRIVE].ob_spec.tedinfo = &ti_cfg;

    set_obj(dlg, OBJ_LBL_CFG_COL, G_STRING, NONE, NORMAL,
            xl + 33*cw, ydrvr, cw, rh);
    dlg[OBJ_LBL_CFG_COL].ob_spec.free_string = ":";

    /* --- Status section --- */
    set_obj(dlg, OBJ_LBL_STATUS, G_STRING, NONE, NORMAL,
            xl, ystat, 6*cw, rh);
    dlg[OBJ_LBL_STATUS].ob_spec.free_string = "Status";

    set_obj(dlg, OBJ_STATUS_TEXT, G_STRING, NONE, NORMAL,
            cw, ystatx, 48*cw, rh);
    dlg[OBJ_STATUS_TEXT].ob_spec.free_string =
        "Active profile is mounted automatically at boot.";

    /* --- Bottom divider --- */
    set_obj(dlg, OBJ_DIV2, G_BOX, NONE, NORMAL,
            cw, ydiv2, DW - 2*cw, 2);
    dlg[OBJ_DIV2].ob_spec.index = 0x00001171L;

    /* --- Buttons (3 x 8*cw, centred in DW=50*cw, left offset = 9*cw) --- */
    set_obj(dlg, OBJ_TEST, G_BUTTON, EXIT | TOUCHEXIT, NORMAL,
             9*cw, ybtn, 8*cw, rh);
    dlg[OBJ_TEST].ob_spec.free_string = "  Test  ";

    set_obj(dlg, OBJ_SAVE, G_BUTTON, EXIT | DEFAULT | TOUCHEXIT, NORMAL,
            21*cw, ybtn, 8*cw, rh);
    dlg[OBJ_SAVE].ob_spec.free_string = "  Save  ";

    set_obj(dlg, OBJ_CANCEL, G_BUTTON, EXIT | TOUCHEXIT, NORMAL,
            33*cw, ybtn, 8*cw, rh);
    dlg[OBJ_CANCEL].ob_spec.free_string = " Cancel ";

    wire_tree(dlg, N_OBJS);

    (void)sx; (void)sy; (void)sw; (void)bw; (void)bh;
}

/* ================================================================== */
/* Populate main dialog buffers from AppConfig                        */
/* ================================================================== */

static void dialog_load_from_config(AppConfig *cfg)
{
    Profile *p;
    char drv[2];
    char port_str[BUF_PORT];

    p = profiles_get_active(cfg);
    if (!p) return;

    set_buf(buf_active,   BUF_LONG, p->name);
    set_buf(buf_name,     BUF_LONG, p->name);
    set_buf(buf_server,   BUF_LONG, p->server);

    sprintf(port_str, "%d", p->port);
    set_buf(buf_port,     BUF_PORT, port_str);

    set_buf(buf_protocol, BUF_LONG, p->protocol);
    set_buf(buf_mount,    BUF_LONG, p->mount);

    if (p->readonly)
        dlg[OBJ_READONLY].ob_state |= (unsigned short)SELECTED;
    else
        dlg[OBJ_READONLY].ob_state &= (unsigned short)(~SELECTED);

    drv[0] = cfg->tnfs_drive;   drv[1] = '\0';
    set_buf(buf_tnfs, BUF_DRV, drv);

    drv[0] = cfg->config_drive; drv[1] = '\0';
    set_buf(buf_cfg, BUF_DRV, drv);
}

/* ================================================================== */
/* Write main dialog buffers back to AppConfig                        */
/* ================================================================== */

static void dialog_save_to_config(AppConfig *cfg)
{
    Profile *p;
    char c;
    int port;

    p = profiles_get_active(cfg);
    if (!p) return;

    buf_copy(buf_name,     p->name,     PROFILE_NAME_LEN);
    buf_copy(buf_server,   p->server,   SERVER_LEN);
    buf_copy(buf_protocol, p->protocol, PROTOCOL_LEN);
    buf_copy(buf_mount,    p->mount,    MOUNT_LEN);

    port = atoi(buf_port);
    if (port < 1 || port > 65535) port = 16384;
    p->port = port;

    p->readonly = (dlg[OBJ_READONLY].ob_state & SELECTED) ? 1 : 0;

    /* Keep buf_active in sync with the (possibly renamed) profile name. */
    set_buf(buf_active, BUF_LONG, p->name);

    /* Drive letters: accept a-z and convert to A-Z. */
    c = buf_tnfs[0];
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    cfg->tnfs_drive = (c >= 'A' && c <= 'Z') ? c : 'N';

    c = buf_cfg[0];
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    cfg->config_drive = (c >= 'A' && c <= 'Z') ? c : 'C';
}

/* ================================================================== */
/* Validate main dialog fields                                         */
/* ================================================================== */

static int dialog_validate(void)
{
    int port;
    char c;

    if (!buf_nonempty(buf_name)) {
        form_alert(1, "[1][Validation error|Profile name is empty.][OK]");
        return 0;
    }
    if (!buf_nonempty(buf_server)) {
        form_alert(1, "[1][Validation error|Server address is empty.][OK]");
        return 0;
    }
    port = atoi(buf_port);
    if (port < 1 || port > 65535) {
        form_alert(1, "[1][Validation error|Port must be 1 - 65535.][OK]");
        return 0;
    }
    if (!buf_nonempty(buf_protocol)) {
        form_alert(1, "[1][Validation error|Protocol is empty.][OK]");
        return 0;
    }
    if (!buf_nonempty(buf_mount)) {
        form_alert(1, "[1][Validation error|Mount dir is empty.][OK]");
        return 0;
    }
    c = buf_tnfs[0];
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c < 'A' || c > 'Z') {
        form_alert(1, "[1][Validation error|TNFS drive: use A - Z.][OK]");
        return 0;
    }
    c = buf_cfg[0];
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c < 'A' || c > 'Z') {
        form_alert(1, "[1][Validation error|Config drive: use A - Z.][OK]");
        return 0;
    }
    form_alert(1, "[1][Test OK|Profile looks valid.][OK]");
    return 1;
}

/* Fase AC-1: minimal GET_CONFIG_INFO probe against the SideTNFS GEMDRIVE
 * firmware, reported with a plain alert. No config fields are read or
 * used here -- this only checks that the cartridge firmware responds. */
static void dialog_probe_firmware(void)
{
    SideTnfsConfigInfo info;
    char msg[80];

    if (sidetnfs_probe_get_config_info(&info) != SIDETNFS_PROBE_OK) {
        form_alert(1, "[3][SideTNFS firmware|not responding (timeout).][OK]");
        return;
    }

    if (info.protocol_version != 1) {
        sprintf(msg, "[3][Unexpected protocol version|Got %lu, expected 1.][OK]",
                info.protocol_version);
        form_alert(1, msg);
        return;
    }

    sprintf(msg, "[1][SideTNFS firmware detected|Protocol: %lu|Server slots: %lu|Configured: %lu][OK]",
            info.protocol_version, info.max_servers, info.server_count);
    form_alert(1, msg);
}

/* ================================================================== */
/* Profile list dialog                                                 */
/* Returns 1 if profile was changed (Use pressed), 0 if cancelled.   */
/* ================================================================== */

static int profile_list_run(AppConfig *cfg)
{
    short cw, ch, bw, bh;
    short sx, sy, sw, sh;
    int rh, tm, row_pitch;
    int DW, DH;
    int yt, ydiv1, yrow0, ydiv2, ybtn;
    int i;
    short x, y, w, h;
    short which;
    int done;
    int selected;

    graf_handle(&cw, &ch, &bw, &bh);
    wind_get(0, WF_WORKXYWH, &sx, &sy, &sw, &sh);
    if (sh <= 0 || sh > 800) sh = 200;

    rh        = (sh >= 350) ? (int)ch : ((ch > 8) ? ch / 2 : (int)ch);
    tm        = (rh / 4 > 2) ? rh / 4 : 2;
    row_pitch = rh + 2;

    DW    = 40 * cw;
    yt    = tm;
    ydiv1 = yt    + rh + 1;
    yrow0 = ydiv1 + 2;
    ydiv2 = yrow0 + MAX_PROFILES * row_pitch + 2;
    ybtn  = ydiv2 + 2 + 2;
    DH    = ybtn  + rh + tm;

    /* --- Build pl_dlg[] --- */
    set_obj(pl_dlg, PL_ROOT, G_BOX, NONE, NORMAL, 0, 0, DW, DH);
    pl_dlg[PL_ROOT].ob_spec.index = 0x00031070L;

    set_obj(pl_dlg, PL_TITLE, G_STRING, NONE, NORMAL,
            9*cw, yt, 22*cw, rh);
    pl_dlg[PL_TITLE].ob_spec.free_string = "  Saved profiles  ";

    set_obj(pl_dlg, PL_DIV1, G_BOX, NONE, NORMAL,
            cw, ydiv1, DW - 2*cw, 2);
    pl_dlg[PL_DIV1].ob_spec.index = 0x00001171L;

    /* --- Build row strings and row objects --- */
    selected = cfg->active_index;
    if (selected < 0 || selected >= cfg->profile_count) selected = 0;

    for (i = 0; i < MAX_PROFILES; i++) {
        int ry    = yrow0 + i * row_pitch;
        int flags = EXIT | TOUCHEXIT;
        int state = (i == selected) ? SELECTED : NORMAL;

        if (i < cfg->profile_count) {
            /* Build "* Name" or "  Name" */
            pl_row_text[i][0] = (i == cfg->active_index) ? '*' : ' ';
            pl_row_text[i][1] = ' ';
            strncpy(pl_row_text[i] + 2,
                    cfg->profiles[i].name,
                    PL_ROW_BUF - 3);
            pl_row_text[i][PL_ROW_BUF - 1] = '\0';
        } else {
            /* Empty slot: non-interactive */
            flags = NONE;
            state = DISABLED;
            pl_row_text[i][0] = '\0';
        }

        set_obj(pl_dlg, PL_ROW_0 + i, G_BUTTON, flags, state,
                cw, ry, DW - 2*cw, rh);
        pl_dlg[PL_ROW_0 + i].ob_spec.free_string = pl_row_text[i];
    }

    set_obj(pl_dlg, PL_DIV2, G_BOX, NONE, NORMAL,
            cw, ydiv2, DW - 2*cw, 2);
    pl_dlg[PL_DIV2].ob_spec.index = 0x00001171L;

    /* 4 buttons across DW=40*cw; each 8*cw, 2*cw gaps, 1*cw left margin */
    set_obj(pl_dlg, PL_USE, G_BUTTON, EXIT | TOUCHEXIT | DEFAULT, NORMAL,
            1*cw, ybtn, 8*cw, rh);
    pl_dlg[PL_USE].ob_spec.free_string = "  Use   ";

    set_obj(pl_dlg, PL_NEW, G_BUTTON, EXIT | TOUCHEXIT, NORMAL,
            11*cw, ybtn, 8*cw, rh);
    pl_dlg[PL_NEW].ob_spec.free_string = "  New   ";

    set_obj(pl_dlg, PL_DELETE, G_BUTTON, EXIT | TOUCHEXIT, NORMAL,
            21*cw, ybtn, 8*cw, rh);
    pl_dlg[PL_DELETE].ob_spec.free_string = " Delete ";

    set_obj(pl_dlg, PL_CANCEL, G_BUTTON, EXIT | TOUCHEXIT, NORMAL,
            31*cw, ybtn, 8*cw, rh);
    pl_dlg[PL_CANCEL].ob_spec.free_string = " Cancel ";

    wire_tree(pl_dlg, PL_NOBJS);

    /* --- Show dialog --- */
    form_center(pl_dlg, &x, &y, &w, &h);
    form_dial(FMD_START, x, y, w, h, x, y, w, h);
    objc_draw(pl_dlg, PL_ROOT, MAX_DEPTH, x, y, w, h);

    done = 0;
    while (!done) {
        which = (short)(form_do(pl_dlg, PL_ROOT) & 0x7FFF);
        wait_mouse_release();

        if (which >= PL_ROW_0 && which <= PL_ROW_7) {
            int row = which - PL_ROW_0;
            if (row < cfg->profile_count) {
                /* Deselect all rows, select the clicked one. */
                for (i = 0; i < MAX_PROFILES; i++)
                    pl_dlg[PL_ROW_0 + i].ob_state &= (unsigned short)(~SELECTED);
                pl_dlg[which].ob_state |= (unsigned short)SELECTED;
                selected = row;
                objc_draw(pl_dlg, PL_ROOT, MAX_DEPTH, x, y, w, h);
            }
            /* continue loop */

        } else if (which == PL_USE) {
            profiles_set_active(cfg, selected);
            done = 2;   /* 2 = profile changed */

        } else if (which == PL_NEW) {
            form_alert(1, "[1][New profile|not implemented yet.][OK]");
            /* continue loop */

        } else if (which == PL_DELETE) {
            form_alert(1, "[1][Delete profile|not implemented yet.][OK]");
            /* continue loop */

        } else {
            done = 1;   /* Cancel or unexpected value */
        }
    }

    form_dial(FMD_FINISH, x, y, w, h, x, y, w, h);

    (void)sx; (void)sy; (void)sw; (void)bw; (void)bh;
    return (done == 2) ? 1 : 0;
}

/* ================================================================== */
/* Main entry point                                                    */
/* ================================================================== */

void dialog_run(AppConfig *cfg)
{
    short x, y, w, h;
    short which;
    int done;

    dialog_init();
    dialog_load_from_config(cfg);

    form_center(dlg, &x, &y, &w, &h);
    form_dial(FMD_START, x, y, w, h, x, y, w, h);
    objc_draw(dlg, OBJ_ROOT, MAX_DEPTH, x, y, w, h);

    done = 0;
    while (!done) {
        which = (short)(form_do(dlg, OBJ_ACTIVE_EDIT) & 0x7FFF);
        wait_mouse_release();

        switch (which) {

        case OBJ_LIST_BTN:
            dlg[OBJ_LIST_BTN].ob_state |= (unsigned short)SELECTED;
            objc_draw(dlg, OBJ_LIST_BTN, MAX_DEPTH, x, y, w, h);
            if (profile_list_run(cfg)) {
                dialog_load_from_config(cfg);
            }
            dlg[OBJ_LIST_BTN].ob_state &= (unsigned short)(~SELECTED);
            objc_draw(dlg, OBJ_ROOT, MAX_DEPTH, x, y, w, h);
            break;

        case OBJ_TEST:
            dlg[OBJ_TEST].ob_state |= (unsigned short)SELECTED;
            objc_draw(dlg, OBJ_TEST, MAX_DEPTH, x, y, w, h);
            if (dialog_validate())
                dialog_probe_firmware();
            dlg[OBJ_TEST].ob_state &= (unsigned short)(~SELECTED);
            objc_draw(dlg, OBJ_TEST, MAX_DEPTH, x, y, w, h);
            break;

        case OBJ_SAVE:
            dlg[OBJ_SAVE].ob_state |= (unsigned short)SELECTED;
            objc_draw(dlg, OBJ_SAVE, MAX_DEPTH, x, y, w, h);
            dialog_save_to_config(cfg);
            if (config_save(cfg, CFG_FILENAME))
                form_alert(1, "[1][Save|Configuration saved.][OK]");
            else
                form_alert(1, "[3][Save error|Could not write|SIDETNFS.CFG][OK]");
            done = 1;
            break;

        case OBJ_CANCEL:
        default:
            done = 1;
            break;
        }
    }

    form_dial(FMD_FINISH, x, y, w, h, x, y, w, h);
}
