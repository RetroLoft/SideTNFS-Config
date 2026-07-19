#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"

#define CFG_LINE_LEN 160

typedef enum { SEC_NONE, SEC_GLOBAL, SEC_PROFILE } cfg_section_t;

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

static char cfg_norm_drive(const char *value, char fallback)
{
    char c = value[0];
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return c;
    return fallback;
}

static int cfg_parse_bool(const char *value, int fallback)
{
    if (cfg_streq_ci(value, "1") || cfg_streq_ci(value, "yes")) return 1;
    if (cfg_streq_ci(value, "0") || cfg_streq_ci(value, "no"))  return 0;
    return fallback;
}

void config_set_defaults(AppConfig *cfg)
{
    profiles_init_defaults(cfg);
}

int config_load(AppConfig *cfg, const char *filename)
{
    FILE *fp;
    char line[CFG_LINE_LEN];
    cfg_section_t section;
    Profile *cur;
    char active_name[PROFILE_NAME_LEN];
    int have_active;

    profiles_init_defaults(cfg);

    fp = fopen(filename, "r");
    if (!fp)
        return 0;

    cfg->profile_count = 0;
    section = SEC_NONE;
    cur = (Profile *)0;
    active_name[0] = '\0';
    have_active = 0;

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

                if (cfg_streq_ci(name, "global")) {
                    section = SEC_GLOBAL;
                    cur = (Profile *)0;
                } else if (cfg_starts_with_ci(name, "profile:")) {
                    section = SEC_PROFILE;
                    if (cfg->profile_count < MAX_PROFILES) {
                        char *pname = name + 8;
                        cfg_trim(pname);
                        cur = &cfg->profiles[cfg->profile_count++];
                        memset(cur, 0, sizeof(*cur));
                        strncpy(cur->name, pname, PROFILE_NAME_LEN - 1);
                        cur->port = 16384;
                        strncpy(cur->protocol, "TNFS", PROTOCOL_LEN - 1);
                        strncpy(cur->mount, "/", MOUNT_LEN - 1);
                        cur->readonly = 0;
                    } else {
                        cur = (Profile *)0;
                    }
                } else {
                    section = SEC_NONE;
                    cur = (Profile *)0;
                }
            }
            continue;
        }

        eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        key = line;
        val = eq + 1;
        cfg_trim(key);
        cfg_trim(val);
        if (key[0] == '\0')
            continue;

        if (section == SEC_GLOBAL) {
            if (cfg_streq_ci(key, "active")) {
                strncpy(active_name, val, PROFILE_NAME_LEN - 1);
                active_name[PROFILE_NAME_LEN - 1] = '\0';
                have_active = 1;
            } else if (cfg_streq_ci(key, "tnfs_drive")) {
                cfg->tnfs_drive = cfg_norm_drive(val, cfg->tnfs_drive);
            } else if (cfg_streq_ci(key, "config_drive")) {
                cfg->config_drive = cfg_norm_drive(val, cfg->config_drive);
            }

        } else if (section == SEC_PROFILE && cur) {
            if (cfg_streq_ci(key, "server")) {
                strncpy(cur->server, val, SERVER_LEN - 1);
                cur->server[SERVER_LEN - 1] = '\0';
            } else if (cfg_streq_ci(key, "port")) {
                int port = atoi(val);
                if (port >= 1 && port <= 65535)
                    cur->port = port;
            } else if (cfg_streq_ci(key, "protocol")) {
                strncpy(cur->protocol, val, PROTOCOL_LEN - 1);
                cur->protocol[PROTOCOL_LEN - 1] = '\0';
            } else if (cfg_streq_ci(key, "mount")) {
                strncpy(cur->mount, val, MOUNT_LEN - 1);
                cur->mount[MOUNT_LEN - 1] = '\0';
            } else if (cfg_streq_ci(key, "readonly")) {
                cur->readonly = cfg_parse_bool(val, cur->readonly);
            }
        }
    }

    fclose(fp);

    if (cfg->profile_count == 0) {
        profiles_init_defaults(cfg);
        return 0;
    }

    cfg->active_index = 0;
    if (have_active) {
        int i;
        for (i = 0; i < cfg->profile_count; i++) {
            if (strcmp(cfg->profiles[i].name, active_name) == 0) {
                cfg->active_index = i;
                break;
            }
        }
    }

    return 1;
}

int config_save(const AppConfig *cfg, const char *filename)
{
    FILE *fp;
    int i;
    const Profile *active;

    fp = fopen(filename, "w");
    if (!fp)
        return 0;

    fprintf(fp, "; SideTNFS configuration file\n");
    fprintf(fp, "; Version 1\n\n");

    active = (cfg->active_index >= 0 && cfg->active_index < cfg->profile_count)
                 ? &cfg->profiles[cfg->active_index] : (const Profile *)0;

    fprintf(fp, "[global]\n");
    fprintf(fp, "active=%s\n", active ? active->name : "");
    fprintf(fp, "tnfs_drive=%c\n", cfg->tnfs_drive);
    fprintf(fp, "config_drive=%c\n", cfg->config_drive);
    fprintf(fp, "\n");

    for (i = 0; i < cfg->profile_count; i++) {
        const Profile *p = &cfg->profiles[i];
        fprintf(fp, "[profile:%s]\n", p->name);
        fprintf(fp, "server=%s\n",   p->server);
        fprintf(fp, "port=%d\n",     p->port);
        fprintf(fp, "protocol=%s\n", p->protocol);
        fprintf(fp, "mount=%s\n",    p->mount);
        fprintf(fp, "readonly=%d\n", p->readonly ? 1 : 0);
        fprintf(fp, "\n");
    }

    fclose(fp);
    return 1;
}

Profile *config_get_active_profile(AppConfig *cfg)
{
    return profiles_get_active(cfg);
}

Profile *config_find_profile(AppConfig *cfg, const char *name)
{
    int i;
    for (i = 0; i < cfg->profile_count; i++) {
        if (strcmp(cfg->profiles[i].name, name) == 0)
            return &cfg->profiles[i];
    }
    return (Profile *)0;
}
