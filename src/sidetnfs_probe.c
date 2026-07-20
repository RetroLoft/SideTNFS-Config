/*
 * Fase AC-4: SideTNFS config-protocol version 2 -- GET_CONFIG_INFO
 * (0x040D), GET_DRIVE (0x040E), SET_DRIVE (0x040F), DELETE_DRIVE
 * (0x0410), SET_CONFIG_DRIVE (0x0411), SAVE_CONFIG (0x0412), plus
 * (Fase 11A/AC-4-2) GET_NETWORK_CONFIG (0x0413), SET_NETWORK_CONFIG
 * (0x0414), SAVE_NETWORK_CONFIG (0x0415), plus (Fase 12A)
 * GET_RTC_CONFIG (0x0416), SET_RTC_CONFIG (0x0417), SAVE_RTC_CONFIG
 * (0x0418).
 *
 * Protocol cross-checked against (read-only references, not modified):
 *   sd2tnfs/docs/sidetnfs-config-protocol.md      (contract, protocol v2)
 *   sd2tnfs/romemul/include/gemdrvemul.h          (offsets, walked from
 *     GEMDRVEMUL_SIDETNFS_CONFIG, not guessed -- re-verified for the
 *     network block: GEMDRVEMUL_SIDETNFS_NETWORK = DRIVE_SD_PATH +
 *     SIDETNFS_SDPATH_LEN = 0x4472, matching this phase's brief exactly,
 *     field by field, through 0x4526; and for the RTC block:
 *     GEMDRVEMUL_SIDETNFS_RTC = ALIGN4(NETWORK_DNS + IPV4_LEN) = 0x4528,
 *     through 0x4572, matching the Fase 12A brief exactly)
 *   sd2tnfs/romemul/include/commands.h            (command codes,
 *     0x0413/0x0414/0x0415/0x0416/0x0417/0x0418 confirmed --
 *     GEMDRVEMUL_SIDETNFS_GET/SET/SAVE_RTC_CONFIG = APP_GEMDRVEMUL<<8 |
 *     0x16/0x17/0x18, APP_GEMDRVEMUL=0x04)
 *   sd2tnfs/romemul/include/sidetnfs_config.h     (drive status/type/
 *     transport enum values)
 *   sd2tnfs/romemul/include/sidetnfs_netconfig.h  (network status enum,
 *     sidetnfs_network_config_t field order/sizes, 176-byte static_assert)
 *   sd2tnfs/romemul/include/sidetnfs_rtcconfig.h  (RTC status enum,
 *     sidetnfs_rtc_config_t field order/sizes, 70-byte static_assert)
 *   sd2tnfs/romemul/include/network.h             (MAX_SSID_LENGTH=36,
 *     MAX_PASSWORD_LENGTH=68, IPV4_ADDRESS_LENGTH=16)
 *   sd2tnfs/romemul/include/memfunc.h             (WRITE_WORD,
 *     WRITE_AND_SWAP_LONGWORD, CHANGE_ENDIANESS_BLOCK16,
 *     COPY_AND_CHANGE_ENDIANESS_BLOCK16, GET_PAYLOAD_PARAM16/32)
 *   sd2tnfs/romemul/gemdrvemul.c                  (all handlers -- exact
 *     payloadPtr consumption order for SET_DRIVE/SET_NETWORK_CONFIG/
 *     SET_RTC_CONFIG (enabled, then ntp_server, then utc_offset), and
 *     confirmation that GET/SET_RTC_CONFIG use the exact same
 *     WRITE_AND_SWAP_LONGWORD/WRITE_WORD/CHANGE_ENDIANESS_BLOCK16 pattern
 *     as the drive/network commands, not a new one)
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

#define CMD_GET_CONFIG_INFO     0x040DUL
#define CMD_GET_DRIVE           0x040EUL
#define CMD_SET_DRIVE           0x040FUL
#define CMD_DELETE_DRIVE        0x0410UL
#define CMD_SET_CONFIG_DRIVE    0x0411UL
#define CMD_SAVE_CONFIG         0x0412UL
#define CMD_GET_NETWORK_CONFIG  0x0413UL
#define CMD_SET_NETWORK_CONFIG  0x0414UL
#define CMD_SAVE_NETWORK_CONFIG 0x0415UL
#define CMD_GET_RTC_CONFIG      0x0416UL
#define CMD_SET_RTC_CONFIG      0x0417UL
#define CMD_SAVE_RTC_CONFIG     0x0418UL

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

/* Fase 11C (Pico alignment fix): the network block's base is no longer a
 * hand-copied literal -- the Pico firmware now computes its own base as
 * ALIGN4(end of drive block), because a uint32_t store to the previous,
 * unaligned 0x4472 status offset was a HardFault (0x4472 is not a
 * multiple of 4). Mirroring the exact same structural computation here,
 * instead of re-copying a new literal, is what keeps the two sides from
 * silently drifting apart again the next time the drive block's size
 * changes. Every field below is derived from the previous field's own
 * offset + length, also matching the Pico side field-by-field. */
#define SIDETNFS_NETWORK_ALIGN4(value) (((value) + 3UL) & ~3UL)

#define NET_BASE_UNALIGNED    (DRIVE_SD_PATH_OFFSET + (unsigned long)SIDETNFS_SDPATH_LEN)
#define NET_STATUS_OFFSET     SIDETNFS_NETWORK_ALIGN4(NET_BASE_UNALIGNED)           /* uint32_t, swapped long */
#define NET_AUTH_MODE_OFFSET  (NET_STATUS_OFFSET     + 4UL)                          /* uint16_t, plain word */
#define NET_USE_DHCP_OFFSET   (NET_AUTH_MODE_OFFSET  + 2UL)                          /* uint16_t, plain word */
#define NET_SSID_OFFSET       (NET_USE_DHCP_OFFSET   + 2UL)                          /* char[36] */
#define NET_PASSWORD_OFFSET   (NET_SSID_OFFSET       + (unsigned long)SIDETNFS_NET_SSID_LEN)     /* char[68] */
#define NET_COUNTRY_OFFSET    (NET_PASSWORD_OFFSET   + (unsigned long)SIDETNFS_NET_PASSWORD_LEN) /* char[4] */
#define NET_IP_ADDRESS_OFFSET (NET_COUNTRY_OFFSET    + (unsigned long)SIDETNFS_NET_COUNTRY_LEN)  /* char[16] */
#define NET_NETMASK_OFFSET    (NET_IP_ADDRESS_OFFSET + (unsigned long)SIDETNFS_NET_IPV4_LEN)     /* char[16] */
#define NET_GATEWAY_OFFSET    (NET_NETMASK_OFFSET    + (unsigned long)SIDETNFS_NET_IPV4_LEN)     /* char[16] */
#define NET_DNS_OFFSET        (NET_GATEWAY_OFFSET    + (unsigned long)SIDETNFS_NET_IPV4_LEN)     /* char[16] */
#define NET_BLOCK_END         (NET_DNS_OFFSET        + (unsigned long)SIDETNFS_NET_IPV4_LEN)

/* Compile-time cross-checks (confirmed to work with this exact toolchain,
 * m68k-atari-mint-gcc 4.6.4, gnu89 defaults -- no -std=c11 needed here).
 * These catch a future field-length change that would silently break
 * hardware alignment or overflow the ROM3 window, without needing to run
 * anything on real hardware to find out. */
_Static_assert((NET_STATUS_OFFSET & 3UL) == 0UL, "NET_STATUS_OFFSET must be 4-byte aligned");
_Static_assert((NET_AUTH_MODE_OFFSET & 1UL) == 0UL, "NET_AUTH_MODE_OFFSET must be 2-byte aligned");
_Static_assert((NET_USE_DHCP_OFFSET & 1UL) == 0UL, "NET_USE_DHCP_OFFSET must be 2-byte aligned");
_Static_assert((NET_SSID_OFFSET & 1UL) == 0UL, "NET_SSID_OFFSET must be 2-byte aligned");
_Static_assert((NET_PASSWORD_OFFSET & 1UL) == 0UL, "NET_PASSWORD_OFFSET must be 2-byte aligned");
_Static_assert((NET_COUNTRY_OFFSET & 1UL) == 0UL, "NET_COUNTRY_OFFSET must be 2-byte aligned");
_Static_assert((NET_IP_ADDRESS_OFFSET & 1UL) == 0UL, "NET_IP_ADDRESS_OFFSET must be 2-byte aligned");
_Static_assert((NET_NETMASK_OFFSET & 1UL) == 0UL, "NET_NETMASK_OFFSET must be 2-byte aligned");
_Static_assert((NET_GATEWAY_OFFSET & 1UL) == 0UL, "NET_GATEWAY_OFFSET must be 2-byte aligned");
_Static_assert((NET_DNS_OFFSET & 1UL) == 0UL, "NET_DNS_OFFSET must be 2-byte aligned");
_Static_assert(NET_BLOCK_END <= 0x10000UL, "network block must fit the 64KB ROM3 window");

/* SET_NETWORK_CONFIG request payload size, excluding the 4-byte token:
 * auth_mode+use_dhcp(2 each=4) + strings (36+68+4+16*4=172) = 176 bytes,
 * matching the brief exactly (no index field here, unlike SET_DRIVE). */
#define SET_NETWORK_CONFIG_PAYLOAD_BYTES \
    (2UL*2UL + (unsigned long)SIDETNFS_NET_SSID_LEN + (unsigned long)SIDETNFS_NET_PASSWORD_LEN + \
     (unsigned long)SIDETNFS_NET_COUNTRY_LEN + 4UL*(unsigned long)SIDETNFS_NET_IPV4_LEN)

/* Fase 12A: the RTC/NTP block immediately follows the network block,
 * same ALIGN4(previous block's own end) structural derivation as the
 * network block used relative to the drive block -- cross-checked
 * against romemul/include/gemdrvemul.h's GEMDRVEMUL_SIDETNFS_RTC_*
 * macros, which compute it exactly the same way (NET_BLOCK_END is
 * already a multiple of 4 here, so ALIGN4 is a no-op in practice, but
 * applying it anyway keeps this resilient to a future network field
 * change the same way the network block itself is resilient to drive
 * block changes). */
#define RTC_BASE_UNALIGNED    NET_BLOCK_END
#define RTC_STATUS_OFFSET     SIDETNFS_NETWORK_ALIGN4(RTC_BASE_UNALIGNED)              /* uint32_t, swapped long */
#define RTC_ENABLED_OFFSET    (RTC_STATUS_OFFSET     + 4UL)                             /* uint16_t, plain word */
#define RTC_NTP_SERVER_OFFSET (RTC_ENABLED_OFFSET    + 2UL)                             /* char[64] */
#define RTC_UTC_OFFSET_OFFSET (RTC_NTP_SERVER_OFFSET + (unsigned long)SIDETNFS_RTC_NTP_SERVER_LEN) /* char[4] */
#define RTC_BLOCK_END          (RTC_UTC_OFFSET_OFFSET + (unsigned long)SIDETNFS_RTC_UTC_OFFSET_LEN)

_Static_assert((RTC_STATUS_OFFSET & 3UL) == 0UL, "RTC_STATUS_OFFSET must be 4-byte aligned");
_Static_assert((RTC_ENABLED_OFFSET & 1UL) == 0UL, "RTC_ENABLED_OFFSET must be 2-byte aligned");
_Static_assert((RTC_NTP_SERVER_OFFSET & 1UL) == 0UL, "RTC_NTP_SERVER_OFFSET must be 2-byte aligned");
_Static_assert((RTC_UTC_OFFSET_OFFSET & 1UL) == 0UL, "RTC_UTC_OFFSET_OFFSET must be 2-byte aligned");
_Static_assert(RTC_BLOCK_END <= 0x10000UL, "RTC block must fit the 64KB ROM3 window");

/* SET_RTC_CONFIG request payload size, excluding the 4-byte token:
 * enabled(2) + ntp_server(64) + utc_offset(4) = 70 bytes, matching the
 * brief exactly (no index field, same shape as SET_NETWORK_CONFIG). */
#define SET_RTC_CONFIG_PAYLOAD_BYTES \
    (2UL + (unsigned long)SIDETNFS_RTC_NTP_SERVER_LEN + (unsigned long)SIDETNFS_RTC_UTC_OFFSET_LEN)

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

int sidetnfs_probe_get_network_config(SideTnfsNetworkConfig *info)
{
    unsigned long seed = send_command_start(CMD_GET_NETWORK_CONFIG, 0UL);

    if (!wait_for_token(seed, PROBE_TIMEOUT_SEC))
        return SIDETNFS_PROBE_TIMEOUT;

    info->status    = rom3_read_long(NET_STATUS_OFFSET);
    info->auth_mode = rom3_read_word(NET_AUTH_MODE_OFFSET);
    info->use_dhcp  = rom3_read_word(NET_USE_DHCP_OFFSET);

    read_string_field(NET_SSID_OFFSET,       info->ssid,        SIDETNFS_NET_SSID_LEN);
    read_string_field(NET_PASSWORD_OFFSET,   info->password,    SIDETNFS_NET_PASSWORD_LEN);
    read_string_field(NET_COUNTRY_OFFSET,    info->country,     SIDETNFS_NET_COUNTRY_LEN);
    read_string_field(NET_IP_ADDRESS_OFFSET, info->ip_address,  SIDETNFS_NET_IPV4_LEN);
    read_string_field(NET_NETMASK_OFFSET,    info->netmask,     SIDETNFS_NET_IPV4_LEN);
    read_string_field(NET_GATEWAY_OFFSET,    info->gateway,     SIDETNFS_NET_IPV4_LEN);
    read_string_field(NET_DNS_OFFSET,        info->primary_dns, SIDETNFS_NET_IPV4_LEN);
    return SIDETNFS_PROBE_OK;
}

int sidetnfs_probe_set_network_config(const SideTnfsNetworkConfig *in, unsigned long *out_status)
{
    unsigned long seed = send_command_start(CMD_SET_NETWORK_CONFIG, SET_NETWORK_CONFIG_PAYLOAD_BYTES);

    /* No index field here, unlike SET_DRIVE -- request starts directly
     * with auth_mode/use_dhcp, matching GEMDRVEMUL_SIDETNFS_SET_NETWORK_CONFIG's
     * payloadPtr consumption order exactly. */
    send_param16(in->auth_mode);
    send_param16(in->use_dhcp);
    send_string_field(in->ssid,        SIDETNFS_NET_SSID_LEN);
    send_string_field(in->password,    SIDETNFS_NET_PASSWORD_LEN);
    send_string_field(in->country,     SIDETNFS_NET_COUNTRY_LEN);
    send_string_field(in->ip_address,  SIDETNFS_NET_IPV4_LEN);
    send_string_field(in->netmask,     SIDETNFS_NET_IPV4_LEN);
    send_string_field(in->gateway,     SIDETNFS_NET_IPV4_LEN);
    send_string_field(in->primary_dns, SIDETNFS_NET_IPV4_LEN);

    if (!wait_for_token(seed, PROBE_TIMEOUT_SEC))
        return SIDETNFS_PROBE_TIMEOUT;

    *out_status = rom3_read_long(NET_STATUS_OFFSET);
    return SIDETNFS_PROBE_OK;
}

int sidetnfs_probe_save_network_config(unsigned long *out_status)
{
    unsigned long seed = send_command_start(CMD_SAVE_NETWORK_CONFIG, 0UL);

    if (!wait_for_token(seed, SAVE_CONFIG_TIMEOUT_SEC))
        return SIDETNFS_PROBE_TIMEOUT;

    *out_status = rom3_read_long(NET_STATUS_OFFSET);
    return SIDETNFS_PROBE_OK;
}

int sidetnfs_probe_get_rtc_config(SideTnfsRtcConfig *info)
{
    unsigned long seed = send_command_start(CMD_GET_RTC_CONFIG, 0UL);

    if (!wait_for_token(seed, PROBE_TIMEOUT_SEC))
        return SIDETNFS_PROBE_TIMEOUT;

    info->status  = rom3_read_long(RTC_STATUS_OFFSET);
    info->enabled = rom3_read_word(RTC_ENABLED_OFFSET);

    read_string_field(RTC_NTP_SERVER_OFFSET, info->ntp_server, SIDETNFS_RTC_NTP_SERVER_LEN);
    read_string_field(RTC_UTC_OFFSET_OFFSET, info->utc_offset, SIDETNFS_RTC_UTC_OFFSET_LEN);
    return SIDETNFS_PROBE_OK;
}

int sidetnfs_probe_set_rtc_config(const SideTnfsRtcConfig *in, unsigned long *out_status)
{
    unsigned long seed = send_command_start(CMD_SET_RTC_CONFIG, SET_RTC_CONFIG_PAYLOAD_BYTES);

    /* No index field here, matching GEMDRVEMUL_SIDETNFS_SET_RTC_CONFIG's
     * payloadPtr consumption order exactly: enabled, then ntp_server,
     * then utc_offset. */
    send_param16(in->enabled);
    send_string_field(in->ntp_server, SIDETNFS_RTC_NTP_SERVER_LEN);
    send_string_field(in->utc_offset, SIDETNFS_RTC_UTC_OFFSET_LEN);

    if (!wait_for_token(seed, PROBE_TIMEOUT_SEC))
        return SIDETNFS_PROBE_TIMEOUT;

    *out_status = rom3_read_long(RTC_STATUS_OFFSET);
    return SIDETNFS_PROBE_OK;
}

int sidetnfs_probe_save_rtc_config(unsigned long *out_status)
{
    unsigned long seed = send_command_start(CMD_SAVE_RTC_CONFIG, 0UL);

    if (!wait_for_token(seed, SAVE_CONFIG_TIMEOUT_SEC))
        return SIDETNFS_PROBE_TIMEOUT;

    *out_status = rom3_read_long(RTC_STATUS_OFFSET);
    return SIDETNFS_PROBE_OK;
}
