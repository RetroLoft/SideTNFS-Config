#ifndef PROFILE_H
#define PROFILE_H

#define MAX_PROFILES      8
#define PROFILE_NAME_LEN 32
#define SERVER_LEN       64
#define PROTOCOL_LEN     16
#define MOUNT_LEN        64

typedef struct {
    char name    [PROFILE_NAME_LEN];
    char server  [SERVER_LEN];
    int  port;
    char protocol[PROTOCOL_LEN];
    char mount   [MOUNT_LEN];
    int  readonly;
} Profile;

typedef struct {
    Profile profiles[MAX_PROFILES];
    int     profile_count;
    int     active_index;
    char    tnfs_drive;
    char    config_drive;
} AppConfig;

void     profiles_init_defaults(AppConfig *cfg);
Profile *profiles_get_active   (AppConfig *cfg);
int      profiles_set_active   (AppConfig *cfg, int index);

#endif
