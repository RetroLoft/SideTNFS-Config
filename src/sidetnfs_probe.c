/*
 * Fase AC-1/AC-2: minimal communication proof with the SideTNFS GEMDRIVE
 * firmware -- GET_CONFIG_INFO (0x040D) and GET_SERVER (0x040E) only. No
 * server records changed, no flash writes, no other commands.
 *
 * Protocol cross-checked against (read-only references, not modified):
 *   sd2tnfs/docs/sidetnfs-config-protocol.md            (contract, incl.
 *     the hardware-proven Fase 9B1 word-order fix and the Fase 9B2
 *     GET_SERVER wire layout)
 *   sd2tnfs/romemul/include/gemdrvemul.h                (offsets/constants)
 *   sd2tnfs/romemul/include/memfunc.h                   (WRITE_WORD,
 *     WRITE_AND_SWAP_LONGWORD, CHANGE_ENDIANESS_BLOCK16, GET_PAYLOAD_PARAM32)
 *   sd2tnfs/romemul/gemdrvemul.c                        (firmware handler,
 *     populate_dta() as the proven precedent for the string fields)
 *   sidecart-gemdrive-atari/src/gemdrive.s +
 *   sidecart-gemdrive-atari/src/inc/sidecart_functions.s (proven 68k-side
 *     random-token handshake and the plain byte-for-byte DTA/filename
 *     copy loop -- no swap needed on the Atari side, see below)
 *   sidecart-configurator-atari/.../helper.c            (send_sync_command,
 *     same handshake shape -- used here as the requested reference, with
 *     the GEMDRVEMUL-specific addresses substituted in)
 *
 * Wire byte-order summary (per the above, not re-derived from assumptions):
 *   - uint32_t fields (WRITE_AND_SWAP_LONGWORD on the Pico side): a plain
 *     32-bit volatile read is correct as-is -- same as GET_CONFIG_INFO,
 *     hardware-verified in Fase 9B1.
 *   - uint16_t fields (WRITE_WORD, no swap): a plain 16-bit volatile read
 *     is correct -- the same convention the production driver already
 *     relies on for arbitrary (non-symmetric) GEMDOS status/error words,
 *     e.g. `move.w GEMDRVEMUL_FCLOSE_STATUS,d0` in gemdrive.s.
 *   - char[] fields (byte-copy + CHANGE_ENDIANESS_BLOCK16 in-place on the
 *     Pico side): read byte-for-byte in address order, no unswap needed on
 *     the Atari side -- identical to how gemdrive.s's
 *     `move.b (a4)+,(a5)+` loop reads populate_dta()'s own
 *     CHANGE_ENDIANESS_BLOCK16-written filename field.
 */

#include <mint/osbind.h>
#include "sidetnfs_probe.h"

#define ROM3_BASE            0xFB0000UL
#define ROM3_PROTOCOL_HEADER 0xABCDUL /* CMD_MAGIC_NUMBER, gemdrive.s */
#define CMD_GET_CONFIG_INFO  0x040DUL /* GEMDRVEMUL_SIDETNFS_GET_CONFIG_INFO */
#define CMD_GET_SERVER       0x040EUL /* GEMDRVEMUL_SIDETNFS_GET_SERVER */

#define RANDOM_TOKEN_OFFSET      0x0000UL /* echoed token, polled after completion */
#define RANDOM_TOKEN_SEED_OFFSET 0x0004UL /* Pico-published seed, read before sending */

#define RESP_PROTOCOL_VERSION_OFFSET 0x4398UL
#define RESP_MAX_SERVERS_OFFSET      0x439CUL
#define RESP_SERVER_COUNT_OFFSET     0x43A0UL
#define RESP_STATUS_OFFSET           0x43A4UL

/* GEMDRVEMUL_SIDETNFS_SERVER = GEMDRVEMUL_SIDETNFS_CONFIG_STATUS + 4 = 0x43A8,
 * per romemul/include/gemdrvemul.h (Fase 9B2). Field offsets below are that
 * header's own running totals, not re-derived. */
#define SERVER_STATUS_OFFSET      0x43A8UL /* uint32_t */
#define SERVER_USED_OFFSET        0x43ACUL /* uint16_t */
#define SERVER_TRANSPORT_OFFSET   0x43AEUL /* uint16_t */
#define SERVER_PORT_OFFSET        0x43B0UL /* uint16_t */
#define SERVER_NICKNAME_OFFSET    0x43B2UL /* char[24] */
#define SERVER_HOST_OFFSET        0x43CAUL /* char[64] */
#define SERVER_MOUNT_PATH_OFFSET  0x440AUL /* char[32], block ends 0x442A */

#define PROBE_TIMEOUT_SEC 2
#define PAL_VBLS_PER_SEC  50 /* Assuming PAL system, as in helper.c */

static unsigned char rom3_read(unsigned long offset)
{
    return *((volatile unsigned char *)(ROM3_BASE + offset));
}

static unsigned short rom3_read_word(unsigned long offset)
{
    return *((volatile unsigned short *)(ROM3_BASE + offset));
}

static unsigned long rom3_read_long(unsigned long offset)
{
    return *((volatile unsigned long *)(ROM3_BASE + offset));
}

/* Bounded Vsync() poll for the firmware to echo `seed` back at
 * RANDOM_TOKEN_OFFSET. Returns 1 on match, 0 on timeout. Shared by both
 * commands -- identical handshake, only the preceding trigger/payload
 * sequence differs. */
static int wait_for_token(unsigned long seed)
{
    unsigned long echoed;
    long budget;

    budget = (long)PROBE_TIMEOUT_SEC * PAL_VBLS_PER_SEC;
    echoed = ~seed; /* force a mismatch before the first check */
    while (budget > 0 && echoed != seed) {
        Vsync();
        echoed = rom3_read_long(RANDOM_TOKEN_OFFSET);
        budget--;
    }
    return echoed == seed;
}

int sidetnfs_probe_get_config_info(SideTnfsConfigInfo *info)
{
    unsigned long seed;

    /* Seed must be captured before the trigger sequence below re-uses the
     * same offset as a discarded trigger read (see sidecart_functions.s:
     * RANDOM_TOKEN_SEED_ADDR is read into d2 before _start_async_code). */
    seed = rom3_read_long(RANDOM_TOKEN_SEED_OFFSET);

    /* Command trigger: the Pico's bus sniffer treats specific ROM3 READ
     * addresses as command signals -- the byte value returned is
     * irrelevant, only the address being read matters. */
    (void)rom3_read(ROM3_PROTOCOL_HEADER);
    (void)rom3_read(CMD_GET_CONFIG_INFO);
    (void)rom3_read(RANDOM_TOKEN_SEED_OFFSET); /* payload size = 0 + 4-byte token */

    /* Send the seed back as two address-encoded reads (low word, then
     * high word) -- same order as send_sync_command()/gemdrive.s. */
    (void)rom3_read(seed & 0xFFFFUL);
    (void)rom3_read((seed >> 16) & 0xFFFFUL);

    if (!wait_for_token(seed))
        return SIDETNFS_PROBE_TIMEOUT;

    info->protocol_version = rom3_read_long(RESP_PROTOCOL_VERSION_OFFSET);
    info->max_servers      = rom3_read_long(RESP_MAX_SERVERS_OFFSET);
    info->server_count     = rom3_read_long(RESP_SERVER_COUNT_OFFSET);
    info->status            = rom3_read_long(RESP_STATUS_OFFSET);
    return SIDETNFS_PROBE_OK;
}

int sidetnfs_probe_get_server(unsigned long server_index, SideTnfsServerInfo *info)
{
    unsigned long seed;
    unsigned long total_payload;
    unsigned int i;

    total_payload = 4UL /* server_index */ + 4UL /* token */;

    seed = rom3_read_long(RANDOM_TOKEN_SEED_OFFSET);

    (void)rom3_read(ROM3_PROTOCOL_HEADER);
    (void)rom3_read(CMD_GET_SERVER);
    (void)rom3_read(total_payload); /* payload size = 4 (server_index) + 4 (token) */

    /* Token first (low, high), then server_index (low, high) -- same
     * register order as send_sync_command_to_sidecart's d2 (token)
     * followed by d3 (first payload word) in sidecart_functions.s, and
     * matching GET_PAYLOAD_PARAM32's (payload[1]<<16)|payload[0]
     * reconstruction in memfunc.h. */
    (void)rom3_read(seed & 0xFFFFUL);
    (void)rom3_read((seed >> 16) & 0xFFFFUL);
    (void)rom3_read(server_index & 0xFFFFUL);
    (void)rom3_read((server_index >> 16) & 0xFFFFUL);

    if (!wait_for_token(seed))
        return SIDETNFS_PROBE_TIMEOUT;

    /* Field by field, in wire order -- no memcpy() of the local struct,
     * since its C padding/alignment is not shown to match the wire
     * layout above. */
    info->status    = rom3_read_long(SERVER_STATUS_OFFSET);
    info->used      = rom3_read_word(SERVER_USED_OFFSET);
    info->transport = rom3_read_word(SERVER_TRANSPORT_OFFSET);
    info->port      = rom3_read_word(SERVER_PORT_OFFSET);

    for (i = 0; i < SIDETNFS_SERVER_NICKNAME_LEN; i++)
        info->nickname[i] = (char)rom3_read(SERVER_NICKNAME_OFFSET + i);
    info->nickname[SIDETNFS_SERVER_NICKNAME_LEN - 1] = '\0';

    for (i = 0; i < SIDETNFS_SERVER_HOST_LEN; i++)
        info->host[i] = (char)rom3_read(SERVER_HOST_OFFSET + i);
    info->host[SIDETNFS_SERVER_HOST_LEN - 1] = '\0';

    for (i = 0; i < SIDETNFS_SERVER_MOUNTPATH_LEN; i++)
        info->mount_path[i] = (char)rom3_read(SERVER_MOUNT_PATH_OFFSET + i);
    info->mount_path[SIDETNFS_SERVER_MOUNTPATH_LEN - 1] = '\0';

    return SIDETNFS_PROBE_OK;
}
