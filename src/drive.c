#include <string.h>
#include "drive.h"

void drive_config_init_defaults(DriveConfig *cfg)
{
    Drive *d;

    memset(cfg, 0, sizeof(*cfg));

    d = &cfg->drives[0];
    d->used   = 1;
    d->letter = 'S';
    d->type   = DRIVE_TYPE_CONFIG;
    strncpy(d->nickname, "Config disk", DRIVE_NICK_LEN - 1);

    d = &cfg->drives[1];
    d->used      = 1;
    d->letter    = 'N';
    d->type      = DRIVE_TYPE_TNFS;
    strncpy(d->nickname, "RetroLoft", DRIVE_NICK_LEN - 1);
    d->transport = DRIVE_TRANSPORT_UDP;
    strncpy(d->host, "192.168.178.10", DRIVE_HOST_LEN - 1);
    d->port = 16384;
    strncpy(d->mount_path, "Atari.ST", DRIVE_MOUNT_LEN - 1);

    cfg->drive_count = 2;

    drive_config_sort_by_letter(cfg);
}

void drive_config_sort_by_letter(DriveConfig *cfg)
{
    int i, j;

    for (i = 1; i < cfg->drive_count; i++) {
        Drive key = cfg->drives[i];
        j = i - 1;
        while (j >= 0 && cfg->drives[j].letter > key.letter) {
            cfg->drives[j + 1] = cfg->drives[j];
            j--;
        }
        cfg->drives[j + 1] = key;
    }
}

int drive_config_config_index(const DriveConfig *cfg)
{
    int i;
    for (i = 0; i < cfg->drive_count; i++)
        if (cfg->drives[i].used && cfg->drives[i].type == DRIVE_TYPE_CONFIG)
            return i;
    return -1;
}

int drive_config_letter_in_use(const DriveConfig *cfg, char letter, int skip_index)
{
    int i;
    for (i = 0; i < cfg->drive_count; i++) {
        if (i == skip_index) continue;
        if (cfg->drives[i].used && cfg->drives[i].letter == letter)
            return 1;
    }
    return 0;
}

int drive_config_letter_valid(const DriveConfig *cfg, char letter, int skip_index)
{
    if (letter < 'A' || letter > 'Z') return 0;
    if (letter == 'A' || letter == 'B') return 0;
    if (drive_config_letter_in_use(cfg, letter, skip_index)) return 0;
    return 1;
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
