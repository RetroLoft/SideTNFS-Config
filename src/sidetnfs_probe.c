/*
 * Fase AC-1: minimal communication proof with the SideTNFS GEMDRIVE
 * firmware -- GET_CONFIG_INFO (0x040D) only. No server records, no flash
 * writes, no other commands.
 *
 * Protocol cross-checked against (read-only references, not modified):
 *   sd2tnfs/docs/sidetnfs-config-protocol.md            (contract)
 *   sd2tnfs/romemul/include/gemdrvemul.h                (offsets/constants)
 *   sd2tnfs/romemul/gemdrvemul.c                        (firmware handler)
 *   sidecart-gemdrive-atari/src/gemdrive.s +
 *   sidecart-gemdrive-atari/src/inc/sidecart_functions.s (proven 68k-side
 *     random-token handshake: RANDOM_TOKEN_SEED_ADDR is read as the seed,
 *     echoed back via two address-encoded reads, then polled for at
 *     RANDOM_TOKEN_ADDR)
 *   sidecart-configurator-atari/.../helper.c            (send_sync_command,
 *     same handshake shape -- used here as the requested reference, with
 *     the GEMDRVEMUL-specific addresses substituted in)
 */

#include <mint/osbind.h>
#include "sidetnfs_probe.h"

#define ROM3_BASE            0xFB0000UL
#define ROM3_PROTOCOL_HEADER 0xABCDUL /* CMD_MAGIC_NUMBER, gemdrive.s */
#define CMD_GET_CONFIG_INFO  0x040DUL /* GEMDRVEMUL_SIDETNFS_GET_CONFIG_INFO */

#define RANDOM_TOKEN_OFFSET      0x0000UL /* echoed token, polled after completion */
#define RANDOM_TOKEN_SEED_OFFSET 0x0004UL /* Pico-published seed, read before sending */

#define RESP_PROTOCOL_VERSION_OFFSET 0x4398UL
#define RESP_MAX_SERVERS_OFFSET      0x439CUL
#define RESP_SERVER_COUNT_OFFSET     0x43A0UL
#define RESP_STATUS_OFFSET           0x43A4UL

#define PROBE_TIMEOUT_SEC 2
#define PAL_VBLS_PER_SEC  50 /* Assuming PAL system, as in helper.c */

static unsigned char rom3_read(unsigned long offset)
{
    return *((volatile unsigned char *)(ROM3_BASE + offset));
}

static unsigned long rom3_read_long(unsigned long offset)
{
    return *((volatile unsigned long *)(ROM3_BASE + offset));
}

int sidetnfs_probe_get_config_info(SideTnfsConfigInfo *info)
{
    unsigned long seed;
    unsigned long echoed;
    long budget;

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

    /* Bounded poll via Vsync() for the firmware to echo the same seed
     * back at RANDOM_TOKEN_OFFSET once GET_CONFIG_INFO has been handled. */
    budget = (long)PROBE_TIMEOUT_SEC * PAL_VBLS_PER_SEC;
    echoed = ~seed; /* force a mismatch before the first check */
    while (budget > 0 && echoed != seed) {
        Vsync();
        echoed = rom3_read_long(RANDOM_TOKEN_OFFSET);
        budget--;
    }
    if (echoed != seed)
        return SIDETNFS_PROBE_TIMEOUT;

    info->protocol_version = rom3_read_long(RESP_PROTOCOL_VERSION_OFFSET);
    info->max_servers      = rom3_read_long(RESP_MAX_SERVERS_OFFSET);
    info->server_count     = rom3_read_long(RESP_SERVER_COUNT_OFFSET);
    info->status            = rom3_read_long(RESP_STATUS_OFFSET);
    return SIDETNFS_PROBE_OK;
}
