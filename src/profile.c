#include <string.h>
#include "profile.h"

void profiles_init_defaults(AppConfig *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    strncpy(cfg->profiles[0].name,     "RetroLoft",          PROFILE_NAME_LEN - 1);
    strncpy(cfg->profiles[0].server,   "192.168.1.10",       SERVER_LEN - 1);
    cfg->profiles[0].port = 16384;
    strncpy(cfg->profiles[0].protocol, "TNFS",               PROTOCOL_LEN - 1);
    strncpy(cfg->profiles[0].mount,    "/ATARI.ST",          MOUNT_LEN - 1);
    cfg->profiles[0].readonly = 1;

    strncpy(cfg->profiles[1].name,     "FujiNet Public",     PROFILE_NAME_LEN - 1);
    strncpy(cfg->profiles[1].server,   "tnfs.fujinet.online",SERVER_LEN - 1);
    cfg->profiles[1].port = 16384;
    strncpy(cfg->profiles[1].protocol, "TNFS",               PROTOCOL_LEN - 1);
    strncpy(cfg->profiles[1].mount,    "/ATARI.ST",          MOUNT_LEN - 1);
    cfg->profiles[1].readonly = 1;

    strncpy(cfg->profiles[2].name,     "Laptop Test",        PROFILE_NAME_LEN - 1);
    strncpy(cfg->profiles[2].server,   "192.168.1.50",       SERVER_LEN - 1);
    cfg->profiles[2].port = 16384;
    strncpy(cfg->profiles[2].protocol, "TNFS",               PROTOCOL_LEN - 1);
    strncpy(cfg->profiles[2].mount,    "/",                  MOUNT_LEN - 1);
    cfg->profiles[2].readonly = 0;

    cfg->profile_count = 3;
    cfg->active_index  = 0;
    cfg->tnfs_drive    = 'N';
    cfg->config_drive  = 'C';
}

Profile *profiles_get_active(AppConfig *cfg)
{
    if (cfg->active_index < 0 || cfg->active_index >= cfg->profile_count)
        return (Profile *)0;
    return &cfg->profiles[cfg->active_index];
}

int profiles_set_active(AppConfig *cfg, int index)
{
    if (index < 0 || index >= cfg->profile_count)
        return 0;
    cfg->active_index = index;
    return 1;
}
