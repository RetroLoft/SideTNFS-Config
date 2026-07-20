#ifndef NETCONFIG_H
#define NETCONFIG_H

/* Fase AC-5: local-only wifi/network settings model. UI state only --
 * no persistence, no firmware communication (see dialog.c's
 * netconfig_editor_run()). No wire protocol exists yet for this data,
 * so field lengths are safe upper bounds for their protocol category
 * (802.11 SSID, WPA/WPA2 passphrase, dotted-quad IPv4), not tied to any
 * firmware struct. */

#define NETCONFIG_SSID_LEN     33 /* 32 bytes + NUL, 802.11 SSID limit */
#define NETCONFIG_PASSWORD_LEN 64 /* 63 chars + NUL, WPA/WPA2 passphrase limit */
#define NETCONFIG_COUNTRY_LEN  3  /* two-letter wifi country code + NUL, e.g. "XX"/"NL"/"DE" */
#define NETCONFIG_IPV4_LEN     16 /* "255.255.255.255" + NUL */

#define NETCONFIG_MODE_DHCP   0
#define NETCONFIG_MODE_STATIC 1

/* Fase AC-4 (network protocol): raw firmware auth_mode value (0-8, see
 * sidetnfs_probe.h). The UI groups these into four canonical choices
 * (Open=0, WPA/TKIP=1, WPA2/AES=3, WPA2 Mixed=6) but only overwrites
 * auth_mode with one of those canonical codes when the user actually
 * changes the selection -- otherwise a previously-read non-canonical
 * code within the same group (e.g. 4) is preserved verbatim. */
typedef struct {
    char ssid[NETCONFIG_SSID_LEN];
    char password[NETCONFIG_PASSWORD_LEN]; /* real value, even though the editor masks it on screen */
    unsigned long auth_mode;
    char country[NETCONFIG_COUNTRY_LEN];

    int  ip_mode; /* NETCONFIG_MODE_* */
    char ip_address[NETCONFIG_IPV4_LEN];
    char netmask[NETCONFIG_IPV4_LEN];
    char gateway[NETCONFIG_IPV4_LEN];
    char dns_server[NETCONFIG_IPV4_LEN];
} NetConfig;

/* In-memory defaults only -- no file/firmware I/O. */
void netconfig_init_defaults(NetConfig *cfg);

#endif
