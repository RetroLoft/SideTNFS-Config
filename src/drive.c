#include <string.h>
#include "drive.h"

int drive_slot_is_empty(const Drive *d)
{
    return d->state == DRIVE_SLOT_EMPTY;
}

int drive_slot_is_configured(const Drive *d)
{
    return d->state == DRIVE_SLOT_DISABLED || d->state == DRIVE_SLOT_ENABLED;
}

int drive_slot_is_enabled(const Drive *d)
{
    return d->state == DRIVE_SLOT_ENABLED;
}

void drive_config_init_defaults(DriveConfig *cfg)
{
    Drive *d;

    memset(cfg, 0, sizeof(*cfg));
    cfg->settings_letter = 'S';

    d = &cfg->drives[0];
    d->state     = DRIVE_SLOT_ENABLED;
    d->letter    = 'N';
    d->type      = DRIVE_TYPE_TNFS;
    strncpy(d->nickname, "RetroLoft", DRIVE_NICK_LEN - 1);
    d->transport = DRIVE_TRANSPORT_UDP;
    strncpy(d->host, "192.168.178.10", DRIVE_HOST_LEN - 1);
    d->port = 16384;
    strncpy(d->mount_path, "Atari.ST", DRIVE_MOUNT_LEN - 1);
    /* drives[1..7] stay EMPTY (state == 0 from the memset above). */
}

int drive_config_configured_count(const DriveConfig *cfg)
{
    int i, n = 0;
    for (i = 0; i < MAX_DRIVES; i++)
        if (drive_slot_is_configured(&cfg->drives[i]))
            n++;
    return n;
}

int drive_config_enabled_count(const DriveConfig *cfg)
{
    int i, n = 0;
    for (i = 0; i < MAX_DRIVES; i++)
        if (drive_slot_is_enabled(&cfg->drives[i]))
            n++;
    return n;
}

int drive_config_empty_count(const DriveConfig *cfg)
{
    int i, n = 0;
    for (i = 0; i < MAX_DRIVES; i++)
        if (drive_slot_is_empty(&cfg->drives[i]))
            n++;
    return n;
}

int drive_config_letter_in_use(const DriveConfig *cfg, char letter, int skip_index)
{
    int i;
    if (cfg->settings_letter == letter) return 1;
    for (i = 0; i < MAX_DRIVES; i++) {
        if (i == skip_index) continue;
        if (drive_slot_is_configured(&cfg->drives[i]) && cfg->drives[i].letter == letter)
            return 1;
    }
    return 0;
}

char drive_config_suggest_letter(const DriveConfig *cfg)
{
    char c;
    for (c = 'C'; c <= 'Z'; c++) {
        if (!drive_config_letter_in_use(cfg, c, -1))
            return c;
    }
    return '\0';
}
