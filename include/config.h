#ifndef CONFIG_H
#define CONFIG_H

#include "drive.h"

/* Fase AC-3: versioned drive-list format, replacing the Fase AC-0
 * profile-based SIDETNFS.CFG. config_load() never crashes on an old or
 * corrupt file: any old `[profile:...]`/`[global]` file, or a `[drive:*]`
 * file that fails validation (missing/duplicate CONFIG drive, duplicate
 * or A/B letters), is safely ignored with a fallback to
 * drive_config_init_defaults(). No partial/half-valid state is ever used,
 * mirroring the firmware's own whole-block validation. */

#define CFG_FILENAME       "SIDETNFS.CFG"
#define CFG_FORMAT_VERSION 2

void config_set_defaults(DriveConfig *cfg);
int  config_load(DriveConfig *cfg, const char *filename);
int  config_save(const DriveConfig *cfg, const char *filename);

#endif
