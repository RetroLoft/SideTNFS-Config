#ifndef CONFIG_H
#define CONFIG_H

#include "profile.h"

/* Reuses the AppConfig/Profile structures from profile.h instead of a
 * separate Config type: they hold exactly the same data, just keyed by
 * active_index rather than an active-profile name string. config_load()
 * resolves the file's "active=Name" key to an index after parsing. */

#define CFG_FILENAME "SIDETNFS.CFG"

void     config_set_defaults      (AppConfig *cfg);
int      config_load              (AppConfig *cfg, const char *filename);
int      config_save              (const AppConfig *cfg, const char *filename);
Profile *config_get_active_profile(AppConfig *cfg);
Profile *config_find_profile      (AppConfig *cfg, const char *name);

#endif
