/*
 * Fase AC-4: SideTNFS config-protocol version 2 -- GET_CONFIG_INFO
 * (0x040D), GET_DRIVE (0x040E), SET_DRIVE (0x040F), DELETE_DRIVE
 * (0x0410), SET_CONFIG_DRIVE (0x0411), SAVE_CONFIG (0x0412).
 *
 * Protocol cross-checked against (read-only references, not modified):
 *   sd2tnfs/docs/sidetnfs-config-protocol.md      (contract, protocol v2)
 *   sd2tnfs/romemul/include/gemdrvemul.h          (offsets, walked from
 *     GEMDRVEMUL_SIDETNFS_CONFIG, not guessed)
 *   sd2tnfs/romemul/include/commands.h            (command codes)
 *   sd2tnfs/romemul/include/sidetnfs_config.h     (status/type/transport
 *     enum values)
 *   sd2tnfs/romemul/include/memfunc.h             (WRITE_WORD,
 *     WRITE_AND_SWAP_LONGWORD, CHANGE_ENDIANESS_BLOCK16,
 *     COPY_AND_CHANGE_ENDIANESS_BLOCK16, GET_PAYLOAD_PARAM16/32)
 *   sd2tnfs/romemul/gemdrvemul.c                  (the six handlers --
 *     exact payloadPtr consumption order for SET_DRIVE)
 *   sidecart-gemdrive-atari/src/inc/sidecart_functions.s
 *     (send_sync_command_to_sidecart / send_sync_write_command_to_sidecart
 *     -- the proven 68k-side random-token handshake AND the proven
 *     string-block send mechanism, both reused verbatim below)
 *
 * Byte-order summary (unchanged reasoning from Fase AC-1/AC-2, extended
 * to the new write commands):
 *   - uint32_t fields (WRITE_AND_SWAP_LONGWORD on the Pico side): a
 *     plain 32-bit volatile read is correct as-is.
 *   - uint16_t fields (WRITE_WORD, no swap): a plain 16-bit volatile
 *     read is correct.
 *   - char[] fields, Pico->Atari (GET_DRIVE): byte-copy +
 *     CHANGE_ENDIANESS_BLOCK16 on the Pico side -- read byte-for-byte in
 *     address order on the Atari side, no unswap needed (same as
 *     populate_dta()'s filename fields).
 *   - char[] fields, Atari->Pico (SET_DRIVE): sent as address-encoded
 *     reads where each 16-bit "address" is two RAW consecutive source
 *     bytes packed big-endian ((b0<<8)|b1) -- exactly what
 *     send_sync_write_command_to_sidecart's even-address word-copy loop
 *     does (`move.w (a4)+,d0` then uses d0 as the read address). The
 *     firmware's COPY_AND_CHANGE_ENDIANESS_BLOCK16 un-swaps this back
 *     into the original byte order on receipt -- verified algebraically
 *     against that macro's definition, not assumed.
 *   - uint16_t request params (SET_DRIVE's used/letter/type/transport/
 *     port): sent as ONE address-encoded read of the raw value, matching
 *     GET_PAYLOAD_PARAM16(payload) = payload[0] (no shift/reassembly).
 *   - uint32_t request params (index, letter): sent as two
 *     address-encoded reads (low word, then high word), matching
 *     GET_PAYLOAD_PARAM32(payload) = (payload[1]<<16)|payload[0].
 */

#include <mint/osbind.h>
#include "sidetnfs_probe.h"

#define ROM3_BASE            0xFB0000UL
#define ROM3_PROTOCOL_HEADER 0xABCDUL /* CMD_MAGIC_NUMBER, gemdrive.s */

#define CMD_GET_CONFIG_INFO  0x040DUL
#define CMD_GET_DRIVE        0x040EUL
#define CMD_SET_DRIVE        0x040FUL
#define CMD_DELETE_DRIVE     0x0410UL
#define CMD_SET_CONFIG_DRIVE 0x0411UL
#define CMD_SAVE_CONFIG      0x0412UL

#define RANDOM_TOKEN_OFFSET      0x0000UL /* echoed token, polled after completion */
#define RANDOM_TOKEN_SEED_OFFSET 0x0004UL /* Pico-published seed, read before sending */

/* GEMDRVEMUL_SIDETNFS_CONFIG = 0x4398 (Fase 9B1, unchanged base). All
 * offsets below are that header's own running totals, walked by hand
 * from gemdrvemul.h -- not independently guessed. */
#define RESP_CONFIG_VERSION_OFFSET      0x4398UL
#define RESP_CONFIG_MAX_DRIVES_OFFSET   0x439CUL
#define RESP_CONFIG_DRIVE_COUNT_OFFSET  0x43A0UL
#define RESP_CONFIG_DRIVE_LETTER_OFFSET 0x43A4UL
#define RESP_CONFIG_STATUS_OFFSET       0x43A8UL

/* GEMDRVEMUL_SIDETNFS_DRIVE = CONFIG_STATUS + 4 = 0x43AC */
#define DRIVE_STATUS_OFFSET      0x43ACUL /* uint32_t */
#define DRIVE_USED_OFFSET        0x43B0UL /* uint16_t */
#define DRIVE_LETTER_OFFSET      0x43B2UL /* uint16_t */
#define DRIVE_TYPE_OFFSET        0x43B4UL /* uint16_t */
#define DRIVE_TRANSPORT_OFFSET   0x43B6UL /* uint16_t */
#define DRIVE_PORT_OFFSET        0x43B8UL /* uint16_t */
#define DRIVE_NICKNAME_OFFSET    0x43BAUL /* char[24] */
#define DRIVE_HOST_OFFSET        0x43D2UL /* char[64] */
#define DRIVE_MOUNT_PATH_OFFSET  0x4412UL /* char[32] */
#define DRIVE_SD_PATH_OFFSET     0x4432UL /* char[64], block ends 0x4472 */

/* SET_DRIVE request payload size, excluding the 4-byte token:
 * index(4) + used+letter+type+transport+port(2 each=10) + strings
 * (24+64+32+64=184) = 198 bytes. */
#define SET_DRIVE_PAYLOAD_BYTES \
    (4UL + 2UL*5UL + (unsigned long)SIDETNFS_NICKNAME_LEN + (unsigned long)SIDETNFS_HOST_LEN + \
     (unsigned long)SIDETNFS_MOUNTPATH_LEN + (unsigned long)SIDETNFS_SDPATH_LEN)

#define PROBE_TIMEOUT_SEC      2
#define SAVE_CONFIG_TIMEOUT_SEC 5 /* SAVE_CONFIG does real flash erase+program */
#define PAL_VBLS_PER_SEC       50 /* Assuming PAL system, as in helper.c */

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
 * RANDOM_TOKEN_OFFSET. Returns 1 on match, 0 on timeout. */
static int wait_for_token(unsigned long seed, int timeout_sec)
{
    unsigned long echoed;
    long budget;

    budget = (long)timeout_sec * PAL_VBLS_PER_SEC;
    echoed = ~seed; /* force a mismatch before the first check */
    while (budget > 0 && echoed != seed) {
        Vsync();
        echoed = rom3_read_long(RANDOM_TOKEN_OFFSET);
        budget--;
    }
    return echoed == seed;
}

/* Seed capture + header/command/payload-size trigger reads + the token
 * itself (low word, then high word) -- identical shape for every
 * command, only `command` and the announced payload size differ. Caller
 * sends any further payload words after this returns, then calls
 * wait_for_token(). total_payload_bytes excludes the token. */
static unsigned long send_command_start(unsigned long command, unsigned long total_payload_bytes)
{
    unsigned long seed = rom3_read_long(RANDOM_TOKEN_SEED_OFFSET);

    (void)rom3_read(ROM3_PROTOCOL_HEADER);
    (void)rom3_read(command);
    (void)rom3_read(total_payload_bytes + 4UL);

    (void)rom3_read(seed & 0xFFFFUL);
    (void)rom3_read((seed >> 16) & 0xFFFFUL);

    return seed;
}

/* One 32-bit request parameter: low word, then high word (matches
 * GET_PAYLOAD_PARAM32). */
static void send_param32(unsigned long value)
{
    (void)rom3_read(value & 0xFFFFUL);
    (void)rom3_read((value >> 16) & 0xFFFFUL);
}

/* One 16-bit request parameter: a single raw-value read (matches
 * GET_PAYLOAD_PARAM16 = payload[0], no reassembly). */
static void send_param16(unsigned long value)
{
    (void)rom3_read(value & 0xFFFFUL);
}

/* A fixed-length string field, sent two raw source bytes at a time,
 * packed big-endian ((b0<<8)|b1), matching
 * send_sync_write_command_to_sidecart's word-copy loop. len must be
 * even (all four string fields are). The firmware's
 * COPY_AND_CHANGE_ENDIANESS_BLOCK16 restores the original byte order. */
static void send_string_field(const char *s, int len)
{
    int i;
    unsigned char b0, b1;

    for (i = 0; i < len; i += 2) {
        b0 = (unsigned char)s[i];
        b1 = (unsigned char)s[i + 1];
        (void)rom3_read(((unsigned long)b0 << 8) | (unsigned long)b1);
    }
}

/* A fixed-length string field, read byte-for-byte in address order (no
 * unswap needed on the Atari side -- see file header). Always forces
 * the destination's last byte to NUL, regardless of what the firmware
 * sent. */
static void read_string_field(unsigned long offset, char *dest, int destsize)
{
    int i;
    for (i = 0; i < destsize; i++)
        dest[i] = (char)rom3_read(offset + (unsigned long)i);
    dest[destsize - 1] = '\0';
}

int sidetnfs_probe_get_config_info(SideTnfsConfigInfo *info)
{
    unsigned long seed = send_command_start(CMD_GET_CONFIG_INFO, 0UL);

    if (!wait_for_token(seed, PROBE_TIMEOUT_SEC))
        return SIDETNFS_PROBE_TIMEOUT;

    info->protocol_version    = rom3_read_long(RESP_CONFIG_VERSION_OFFSET);
    info->max_drives          = rom3_read_long(RESP_CONFIG_MAX_DRIVES_OFFSET);
    info->drive_count         = rom3_read_long(RESP_CONFIG_DRIVE_COUNT_OFFSET);
    info->config_drive_letter = rom3_read_long(RESP_CONFIG_DRIVE_LETTER_OFFSET);
    info->status               = rom3_read_long(RESP_CONFIG_STATUS_OFFSET);
    return SIDETNFS_PROBE_OK;
}

int sidetnfs_probe_get_drive(unsigned long index, SideTnfsDriveInfo *info)
{
    unsigned long seed = send_command_start(CMD_GET_DRIVE, 4UL);
    send_param32(index);

    if (!wait_for_token(seed, PROBE_TIMEOUT_SEC))
        return SIDETNFS_PROBE_TIMEOUT;

    info->status    = rom3_read_long(DRIVE_STATUS_OFFSET);
    info->used      = rom3_read_word(DRIVE_USED_OFFSET);
    info->letter    = rom3_read_word(DRIVE_LETTER_OFFSET);
    info->type      = rom3_read_word(DRIVE_TYPE_OFFSET);
    info->transport = rom3_read_word(DRIVE_TRANSPORT_OFFSET);
    info->port      = rom3_read_word(DRIVE_PORT_OFFSET);

    read_string_field(DRIVE_NICKNAME_OFFSET,   info->nickname,   SIDETNFS_NICKNAME_LEN);
    read_string_field(DRIVE_HOST_OFFSET,       info->host,       SIDETNFS_HOST_LEN);
    read_string_field(DRIVE_MOUNT_PATH_OFFSET, info->mount_path, SIDETNFS_MOUNTPATH_LEN);
    read_string_field(DRIVE_SD_PATH_OFFSET,    info->sd_path,    SIDETNFS_SDPATH_LEN);
    return SIDETNFS_PROBE_OK;
}

int sidetnfs_probe_set_drive(unsigned long index, const SideTnfsDriveInfo *in, unsigned long *out_status)
{
    unsigned long seed = send_command_start(CMD_SET_DRIVE, SET_DRIVE_PAYLOAD_BYTES);

    send_param32(index);
    send_param16(in->used);
    send_param16(in->letter);
    send_param16(in->type);
    send_param16(in->transport);
    send_param16(in->port);
    send_string_field(in->nickname,   SIDETNFS_NICKNAME_LEN);
    send_string_field(in->host,       SIDETNFS_HOST_LEN);
    send_string_field(in->mount_path, SIDETNFS_MOUNTPATH_LEN);
    send_string_field(in->sd_path,    SIDETNFS_SDPATH_LEN);

    if (!wait_for_token(seed, PROBE_TIMEOUT_SEC))
        return SIDETNFS_PROBE_TIMEOUT;

    *out_status = rom3_read_long(DRIVE_STATUS_OFFSET);
    return SIDETNFS_PROBE_OK;
}

int sidetnfs_probe_delete_drive(unsigned long index, unsigned long *out_status)
{
    unsigned long seed = send_command_start(CMD_DELETE_DRIVE, 4UL);
    send_param32(index);

    if (!wait_for_token(seed, PROBE_TIMEOUT_SEC))
        return SIDETNFS_PROBE_TIMEOUT;

    *out_status = rom3_read_long(DRIVE_STATUS_OFFSET);
    return SIDETNFS_PROBE_OK;
}

int sidetnfs_probe_set_config_drive(unsigned long letter, unsigned long *out_status)
{
    unsigned long seed = send_command_start(CMD_SET_CONFIG_DRIVE, 4UL);
    send_param32(letter);

    if (!wait_for_token(seed, PROBE_TIMEOUT_SEC))
        return SIDETNFS_PROBE_TIMEOUT;

    *out_status = rom3_read_long(DRIVE_STATUS_OFFSET);
    return SIDETNFS_PROBE_OK;
}

int sidetnfs_probe_save_config(unsigned long *out_status)
{
    unsigned long seed = send_command_start(CMD_SAVE_CONFIG, 0UL);

    if (!wait_for_token(seed, SAVE_CONFIG_TIMEOUT_SEC))
        return SIDETNFS_PROBE_TIMEOUT;

    *out_status = rom3_read_long(DRIVE_STATUS_OFFSET);
    return SIDETNFS_PROBE_OK;
}
