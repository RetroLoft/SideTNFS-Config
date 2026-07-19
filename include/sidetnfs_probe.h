#ifndef SIDETNFS_PROBE_H
#define SIDETNFS_PROBE_H

typedef struct {
    unsigned long protocol_version;
    unsigned long max_servers;
    unsigned long server_count;
    unsigned long status;
} SideTnfsConfigInfo;

#define SIDETNFS_SERVER_NICKNAME_LEN   24
#define SIDETNFS_SERVER_HOST_LEN       64
#define SIDETNFS_SERVER_MOUNTPATH_LEN  32

typedef struct {
    unsigned long status; /* SIDETNFS_SERVER_STATUS_* */
    unsigned long used;
    unsigned long transport; /* 0 = UDP, 1 = TCP */
    unsigned long port;
    char nickname[SIDETNFS_SERVER_NICKNAME_LEN];
    char host[SIDETNFS_SERVER_HOST_LEN];
    char mount_path[SIDETNFS_SERVER_MOUNTPATH_LEN];
} SideTnfsServerInfo;

#define SIDETNFS_SERVER_STATUS_OK            0
#define SIDETNFS_SERVER_STATUS_INVALID_INDEX 1
#define SIDETNFS_SERVER_STATUS_EMPTY_SLOT    2

#define SIDETNFS_PROBE_OK      0
#define SIDETNFS_PROBE_TIMEOUT 1

/* Fase AC-1: minimal GET_CONFIG_INFO (0x040D) probe against the active
 * SideTNFS GEMDRIVE firmware. Returns SIDETNFS_PROBE_OK with *info filled
 * in, or SIDETNFS_PROBE_TIMEOUT if no reply arrived within the bounded
 * poll. Read-only: no server records, no flash writes, no other commands. */
int sidetnfs_probe_get_config_info(SideTnfsConfigInfo *info);

/* Fase AC-2: GET_SERVER (0x040E) probe for a single server slot. Returns
 * SIDETNFS_PROBE_OK with *info filled in (check info->status for
 * OK/INVALID_INDEX/EMPTY_SLOT) or SIDETNFS_PROBE_TIMEOUT on no reply.
 * Read-only: no server records changed, no flash writes. */
int sidetnfs_probe_get_server(unsigned long server_index, SideTnfsServerInfo *info);

#endif
