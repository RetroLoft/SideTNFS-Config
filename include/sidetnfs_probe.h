#ifndef SIDETNFS_PROBE_H
#define SIDETNFS_PROBE_H

typedef struct {
    unsigned long protocol_version;
    unsigned long max_servers;
    unsigned long server_count;
    unsigned long status;
} SideTnfsConfigInfo;

#define SIDETNFS_PROBE_OK      0
#define SIDETNFS_PROBE_TIMEOUT 1

/* Fase AC-1: minimal GET_CONFIG_INFO (0x040D) probe against the active
 * SideTNFS GEMDRIVE firmware. Returns SIDETNFS_PROBE_OK with *info filled
 * in, or SIDETNFS_PROBE_TIMEOUT if no reply arrived within the bounded
 * poll. Read-only: no server records, no flash writes, no other commands. */
int sidetnfs_probe_get_config_info(SideTnfsConfigInfo *info);

#endif
