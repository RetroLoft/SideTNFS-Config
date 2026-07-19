#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"

#define CFG_LINE_LEN 160

static void cfg_trim(char *s)
{
    int n = (int)strlen(s);
    int i = 0;

    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' ||
                     s[n-1] == ' '  || s[n-1] == '\t'))
        s[--n] = '\0';

    while (s[i] == ' ' || s[i] == '\t')
        i++;
    if (i > 0)
        memmove(s, s + i, (size_t)(n - i + 1));
}

static int cfg_streq_ci(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
        a++; b++;
    }
    return (*a == '\0' && *b == '\0');
}

static int cfg_starts_with_ci(const char *s, const char *prefix)
{
    while (*prefix) {
        char cs = *s, cp = *prefix;
        if (cs >= 'A' && cs <= 'Z') cs = (char)(cs - 'A' + 'a');
        if (cp >= 'A' && cp <= 'Z') cp = (char)(cp - 'A' + 'a');
        if (cs != cp) return 0;
        s++; prefix++;
    }
    return 1;
}

static char cfg_upper1(const char *value)
{
    char c = value[0];
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    return c;
}

/* Whole-parsed-file validation: mirrors the firmware's own "any invalid
 * step rejects the entire block" philosophy (see
 * sidetnfs-config-protocol.md, Fase 9B2 loading order) -- never use a
 * half-valid drive list. */
static int cfg_validate(const DriveConfig *cfg)
{
    int i, j;
    int config_count = 0;

    if (cfg->drive_count <= 0 || cfg->drive_count > MAX_DRIVES)
        return 0;

    for (i = 0; i < cfg->drive_count; i++) {
        const Drive *d = &cfg->drives[i];
        if (!d->used)
            return 0;
        if (d->letter < 'A' || d->letter > 'Z')
            return 0;
        if (d->letter == 'A' || d->letter == 'B')
            return 0;
        if (d->type == DRIVE_TYPE_CONFIG)
            config_count++; /* exactly one CONFIG drive, position not fixed */
        for (j = 0; j < i; j++)
            if (cfg->drives[j].letter == d->letter)
                return 0;
    }
    return config_count == 1;
}

void config_set_defaults(DriveConfig *cfg)
{
    drive_config_init_defaults(cfg);
}

int config_load(DriveConfig *cfg, const char *filename)
{
    FILE *fp;
    char line[CFG_LINE_LEN];
    DriveConfig tmp;
    Drive *cur;

    fp = fopen(filename, "r");
    if (!fp) {
        drive_config_init_defaults(cfg);
        return 0;
    }

    memset(&tmp, 0, sizeof(tmp));
    cur = (Drive *)0;

    while (fgets(line, CFG_LINE_LEN, fp)) {
        char *eq;
        char *key;
        char *val;

        cfg_trim(line);

        if (line[0] == '\0' || line[0] == ';' || line[0] == '#')
            continue;

        if (line[0] == '[') {
            char *end = strchr(line, ']');
            if (end) {
                char *name = line + 1;
                *end = '\0';
                cfg_trim(name);

                if (cfg_starts_with_ci(name, "drive:") && tmp.drive_count < MAX_DRIVES) {
                    char *letter = name + 6;
                    cfg_trim(letter);
                    cur = &tmp.drives[tmp.drive_count++];
                    memset(cur, 0, sizeof(*cur));
                    cur->used   = 1;
                    cur->letter = cfg_upper1(letter);
                    cur->transport = DRIVE_TRANSPORT_UDP;
                    cur->port      = 16384;
                } else {
                    /* Unrecognized section (old [global]/[profile:...],
                     * or a drive: section past MAX_DRIVES) -- ignore its
                     * keys rather than misinterpreting them. */
                    cur = (Drive *)0;
                }
            }
            continue;
        }

        eq = strchr(line, '=');
        if (!eq || !cur)
            continue;
        *eq = '\0';
        key = line;
        val = eq + 1;
        cfg_trim(key);
        cfg_trim(val);
        if (key[0] == '\0')
            continue;

        if (cfg_streq_ci(key, "type")) {
            if (cfg_streq_ci(val, "CONFIG"))     cur->type = DRIVE_TYPE_CONFIG;
            else if (cfg_streq_ci(val, "SD"))    cur->type = DRIVE_TYPE_SD;
            else if (cfg_streq_ci(val, "TNFS"))  cur->type = DRIVE_TYPE_TNFS;
        } else if (cfg_streq_ci(key, "nickname")) {
            strncpy(cur->nickname, val, DRIVE_NICK_LEN - 1);
            cur->nickname[DRIVE_NICK_LEN - 1] = '\0';
        } else if (cfg_streq_ci(key, "transport")) {
            cur->transport = cfg_streq_ci(val, "TCP") ? DRIVE_TRANSPORT_TCP : DRIVE_TRANSPORT_UDP;
        } else if (cfg_streq_ci(key, "host")) {
            strncpy(cur->host, val, DRIVE_HOST_LEN - 1);
            cur->host[DRIVE_HOST_LEN - 1] = '\0';
        } else if (cfg_streq_ci(key, "port")) {
            int port = atoi(val);
            if (port >= 1 && port <= 65535)
                cur->port = port;
        } else if (cfg_streq_ci(key, "mount_path")) {
            strncpy(cur->mount_path, val, DRIVE_MOUNT_LEN - 1);
            cur->mount_path[DRIVE_MOUNT_LEN - 1] = '\0';
        } else if (cfg_streq_ci(key, "sd_path")) {
            strncpy(cur->sd_path, val, DRIVE_SDPATH_LEN - 1);
            cur->sd_path[DRIVE_SDPATH_LEN - 1] = '\0';
        }
    }

    fclose(fp);

    if (tmp.drive_count == 0) {
        drive_config_init_defaults(cfg);
        return 0;
    }

    /* Always list drives in driveletter order, regardless of on-disk
     * order -- the config drive's position is no longer fixed. */
    drive_config_sort_by_letter(&tmp);

    if (!cfg_validate(&tmp)) {
        drive_config_init_defaults(cfg);
        return 0;
    }

    *cfg = tmp;
    return 1;
}

int config_save(const DriveConfig *cfg, const char *filename)
{
    FILE *fp;
    int i;

    fp = fopen(filename, "w");
    if (!fp)
        return 0;

    fprintf(fp, "; SideTNFS configuration file\n");
    fprintf(fp, "; Version %d\n\n", CFG_FORMAT_VERSION);

    for (i = 0; i < cfg->drive_count; i++) {
        const Drive *d = &cfg->drives[i];
        if (!d->used)
            continue;

        fprintf(fp, "[drive:%c]\n", d->letter);
        switch (d->type) {
        case DRIVE_TYPE_CONFIG:
            fprintf(fp, "type=CONFIG\n");
            fprintf(fp, "nickname=%s\n", d->nickname);
            break;
        case DRIVE_TYPE_TNFS:
            fprintf(fp, "type=TNFS\n");
            fprintf(fp, "nickname=%s\n", d->nickname);
            fprintf(fp, "transport=%s\n", d->transport == DRIVE_TRANSPORT_TCP ? "TCP" : "UDP");
            fprintf(fp, "host=%s\n", d->host);
            fprintf(fp, "port=%d\n", d->port);
            fprintf(fp, "mount_path=%s\n", d->mount_path);
            break;
        case DRIVE_TYPE_SD:
            fprintf(fp, "type=SD\n");
            fprintf(fp, "nickname=%s\n", d->nickname);
            fprintf(fp, "sd_path=%s\n", d->sd_path);
            break;
        }
        fprintf(fp, "\n");
    }

    fclose(fp);
    return 1;
}
