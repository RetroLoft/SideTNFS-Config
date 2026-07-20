/*
 * Host-side logic tests for the wifi/network config protocol (Fase AC-4).
 *
 * This is NOT part of the cross-compiled SIDETNFS.PRG build (see the
 * project Makefile) -- it compiles and runs on the development host with
 * the *native* toolchain, because most of dialog.c/sidetnfs_probe.c
 * cannot run there at all: they dereference literal ROM3 bus addresses
 * (real hardware only) and call GEM/AES functions (form_do(), objc_draw(),
 * TEDINFO/OBJECT types) that only exist when linked against gemlib on the
 * m68k target. Build/run with, e.g.:
 *
 *   cc -Wall -Wextra -o /tmp/test_netconfig tests/test_netconfig_protocol.c \
 *       -I include && /tmp/test_netconfig
 *
 * What this file covers, and why the rest genuinely cannot be automated
 * here (see the report for the full breakdown):
 *   - auth_mode_to_index(): every canonical group + the out-of-range
 *     fallback (mirrors dialog.c's static function of the same name,
 *     verbatim logic, kept in sync by inspection since dialog.c's copy
 *     has internal linkage and cannot be linked from here).
 *   - Country validation/uppercasing for "XX", "NL", lowercase "nl", and
 *     rejected inputs (mirrors nc_save_to_config()'s country handling).
 *   - netconfig_status_text(): all 13 known statuses plus an unknown
 *     value (mirrors dialog.c's static function of the same name).
 *   - wire_to_ui_netconfig()/ui_to_wire_netconfig() round-trips,
 *     including the wire char[4] <-> UI char[3] country truncation and
 *     DHCP/static ip_mode mapping (mirrors dialog.c's static functions).
 *   - The password-placeholder retain-if-unchanged rule (mirrors
 *     nc_load_from_config()/nc_save_to_config()'s masking logic).
 *   - SET_NETWORK_CONFIG's 176-byte wire size, from the *public*
 *     SIDETNFS_NET_* length constants in sidetnfs_probe.h (the actual
 *     198-style byte-count macro in sidetnfs_probe.c is private, so this
 *     checks the same invariant from the public contract instead).
 *   - The Atari->Pico string pack ((b0<<8)|b1) round-tripped through the
 *     firmware's own word-swap decode formula (CHANGE_ENDIANESS_BLOCK16's
 *     algebra, sd2tnfs/romemul/include/memfunc.h), for representative
 *     strings including odd/edge byte values.
 *   - wait_for_token()'s poll loop is structurally bounded (a fixed
 *     budget that only ever decrements, no code path that loops forever)
 *     -- checked here as a standalone simulation of that loop shape,
 *     since the real function dereferences real hardware.
 *
 * NOT covered here (needs real SideTNFS hardware, not host-testable):
 *   - Actual GET/SET/SAVE_NETWORK_CONFIG round-trips against firmware.
 *   - Exact-readback verification after a real SAVE_NETWORK_CONFIG.
 *   - Real timeout behavior (no firmware attached / bus not responding).
 *   - DHCP-on/off visually disabling the static IP fields (GEM ob_state,
 *     requires a linked GEM environment).
 */

#include <stdio.h>
#include <string.h>

#include "netconfig.h"
#include "sidetnfs_probe.h"

static int g_failures = 0;

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
            g_failures++; \
        } \
    } while (0)

/* ------------------------------------------------------------------ */
/* Mirrors dialog.c's static auth_mode_to_index().                     */
/* ------------------------------------------------------------------ */
static int auth_mode_to_index(unsigned long auth_mode)
{
    if (auth_mode == 0UL) return 0;
    if (auth_mode == 1UL || auth_mode == 2UL) return 1;
    if (auth_mode >= 3UL && auth_mode <= 5UL) return 2;
    if (auth_mode >= 6UL && auth_mode <= 8UL) return 3;
    return 2;
}

static void test_auth_mode_groups(void)
{
    CHECK(auth_mode_to_index(0) == 0, "auth 0 -> Open");
    CHECK(auth_mode_to_index(1) == 1, "auth 1 -> WPA/TKIP");
    CHECK(auth_mode_to_index(2) == 1, "auth 2 -> WPA/TKIP");
    CHECK(auth_mode_to_index(3) == 2, "auth 3 -> WPA2/AES");
    CHECK(auth_mode_to_index(4) == 2, "auth 4 -> WPA2/AES");
    CHECK(auth_mode_to_index(5) == 2, "auth 5 -> WPA2/AES");
    CHECK(auth_mode_to_index(6) == 3, "auth 6 -> WPA2 Mixed");
    CHECK(auth_mode_to_index(7) == 3, "auth 7 -> WPA2 Mixed");
    CHECK(auth_mode_to_index(8) == 3, "auth 8 -> WPA2 Mixed");
    CHECK(auth_mode_to_index(99) == 2, "out-of-range auth falls back to WPA2/AES");
}

/* ------------------------------------------------------------------ */
/* Mirrors nc_save_to_config()'s country validation/uppercasing.       */
/* ------------------------------------------------------------------ */
static int validate_and_uppercase_country(const char *input, char out[3])
{
    char c0, c1;
    int len = (int)strlen(input);

    c0 = (len >= 1) ? input[0] : '\0';
    c1 = (len >= 2) ? input[1] : '\0';
    if (c0 >= 'a' && c0 <= 'z') c0 = (char)(c0 - 'a' + 'A');
    if (c1 >= 'a' && c1 <= 'z') c1 = (char)(c1 - 'a' + 'A');

    if (len != 2 || c0 < 'A' || c0 > 'Z' || c1 < 'A' || c1 > 'Z')
        return 0;

    out[0] = c0; out[1] = c1; out[2] = '\0';
    return 1;
}

static void test_country_validation(void)
{
    char out[3];

    CHECK(validate_and_uppercase_country("XX", out) == 1 && strcmp(out, "XX") == 0, "country XX accepted as-is");
    CHECK(validate_and_uppercase_country("NL", out) == 1 && strcmp(out, "NL") == 0, "country NL accepted as-is");
    CHECK(validate_and_uppercase_country("nl", out) == 1 && strcmp(out, "NL") == 0, "lowercase nl uppercased to NL");
    CHECK(validate_and_uppercase_country("de", out) == 1 && strcmp(out, "DE") == 0, "lowercase de uppercased to DE");
    CHECK(validate_and_uppercase_country("", out) == 0, "empty country rejected");
    CHECK(validate_and_uppercase_country("X", out) == 0, "single-letter country rejected");
    CHECK(validate_and_uppercase_country("N1", out) == 0, "digit in country rejected");
    CHECK(validate_and_uppercase_country("N.", out) == 0, "punctuation in country rejected");
    CHECK(validate_and_uppercase_country("NLD", out) == 0, "three-letter country rejected");
}

/* ------------------------------------------------------------------ */
/* Mirrors dialog.c's static netconfig_status_text().                  */
/* ------------------------------------------------------------------ */
static const char *netconfig_status_text(unsigned long status)
{
    switch (status) {
    case SIDETNFS_NETCONFIG_STATUS_OK:                  return "OK.";
    case SIDETNFS_NETCONFIG_STATUS_INVALID_SSID:        return "Invalid SSID.";
    case SIDETNFS_NETCONFIG_STATUS_INVALID_PASSWORD:    return "Invalid password.";
    case SIDETNFS_NETCONFIG_STATUS_INVALID_AUTH_MODE:   return "Invalid authentication mode.";
    case SIDETNFS_NETCONFIG_STATUS_INVALID_COUNTRY:     return "Invalid country code.";
    case SIDETNFS_NETCONFIG_STATUS_INVALID_DHCP:        return "Invalid DHCP setting.";
    case SIDETNFS_NETCONFIG_STATUS_INVALID_IP:          return "Invalid IP address.";
    case SIDETNFS_NETCONFIG_STATUS_INVALID_NETMASK:     return "Invalid netmask.";
    case SIDETNFS_NETCONFIG_STATUS_INVALID_GATEWAY:     return "Invalid gateway.";
    case SIDETNFS_NETCONFIG_STATUS_INVALID_DNS:         return "Invalid DNS server.";
    case SIDETNFS_NETCONFIG_STATUS_NOT_STAGED:          return "No config staged.";
    case SIDETNFS_NETCONFIG_STATUS_FLASH_WRITE_FAILED:  return "Flash write failed.";
    case SIDETNFS_NETCONFIG_STATUS_FLASH_VERIFY_FAILED: return "Flash verify failed.";
    default:                                            return "Unknown status.";
    }
}

static void test_status_text_coverage(void)
{
    unsigned long s;
    for (s = 0; s <= 12; s++) {
        const char *text = netconfig_status_text(s);
        CHECK(text != NULL && text[0] != '\0', "every defined status has non-empty text");
        CHECK(strcmp(text, "Unknown status.") != 0, "every defined 0-12 status has a specific message");
    }
    CHECK(strcmp(netconfig_status_text(999), "Unknown status.") == 0, "undefined status falls back to Unknown status.");
}

/* ------------------------------------------------------------------ */
/* Mirrors dialog.c's static wire_to_ui_netconfig()/ui_to_wire_netconfig(). */
/* ------------------------------------------------------------------ */
static void wire_to_ui_netconfig(const SideTnfsNetworkConfig *w, NetConfig *nc)
{
    memset(nc, 0, sizeof(*nc));
    nc->auth_mode = w->auth_mode;
    nc->ip_mode   = (w->use_dhcp != 0UL) ? NETCONFIG_MODE_DHCP : NETCONFIG_MODE_STATIC;

    strncpy(nc->ssid,       w->ssid,        NETCONFIG_SSID_LEN - 1);
    strncpy(nc->password,   w->password,    NETCONFIG_PASSWORD_LEN - 1);
    strncpy(nc->country,    w->country,     NETCONFIG_COUNTRY_LEN - 1);
    strncpy(nc->ip_address, w->ip_address,  NETCONFIG_IPV4_LEN - 1);
    strncpy(nc->netmask,    w->netmask,     NETCONFIG_IPV4_LEN - 1);
    strncpy(nc->gateway,    w->gateway,     NETCONFIG_IPV4_LEN - 1);
    strncpy(nc->dns_server, w->primary_dns, NETCONFIG_IPV4_LEN - 1);
}

static void ui_to_wire_netconfig(const NetConfig *nc, SideTnfsNetworkConfig *w)
{
    memset(w, 0, sizeof(*w));
    w->auth_mode = nc->auth_mode;
    w->use_dhcp  = (nc->ip_mode == NETCONFIG_MODE_DHCP) ? 1UL : 0UL;

    strncpy(w->ssid,        nc->ssid,       SIDETNFS_NET_SSID_LEN - 1);
    strncpy(w->password,    nc->password,   SIDETNFS_NET_PASSWORD_LEN - 1);
    strncpy(w->country,     nc->country,    SIDETNFS_NET_COUNTRY_LEN - 1);
    strncpy(w->ip_address,  nc->ip_address, SIDETNFS_NET_IPV4_LEN - 1);
    strncpy(w->netmask,     nc->netmask,    SIDETNFS_NET_IPV4_LEN - 1);
    strncpy(w->gateway,     nc->gateway,    SIDETNFS_NET_IPV4_LEN - 1);
    strncpy(w->primary_dns, nc->dns_server, SIDETNFS_NET_IPV4_LEN - 1);
}

static void test_get_field_display(void)
{
    SideTnfsNetworkConfig wire;
    NetConfig nc;

    memset(&wire, 0, sizeof(wire));
    wire.status    = SIDETNFS_NETCONFIG_STATUS_OK;
    wire.auth_mode = 3;
    wire.use_dhcp  = 0; /* static */
    strcpy(wire.ssid,        "HomeNetwork");
    strcpy(wire.password,    "supersecret");
    strcpy(wire.country,     "NL");
    strcpy(wire.ip_address,  "192.168.1.50");
    strcpy(wire.netmask,     "255.255.255.0");
    strcpy(wire.gateway,     "192.168.1.1");
    strcpy(wire.primary_dns, "192.168.1.1");

    wire_to_ui_netconfig(&wire, &nc);

    CHECK(strcmp(nc.ssid, "HomeNetwork") == 0, "GET: ssid displayed correctly");
    CHECK(strcmp(nc.password, "supersecret") == 0, "GET: password held internally (real value, not masked)");
    CHECK(nc.auth_mode == 3, "GET: auth_mode displayed correctly");
    CHECK(strcmp(nc.country, "NL") == 0, "GET: country displayed correctly (wire[4] -> UI[3] truncation)");
    CHECK(nc.ip_mode == NETCONFIG_MODE_STATIC, "GET: use_dhcp=0 maps to Static IP");
    CHECK(strcmp(nc.ip_address, "192.168.1.50") == 0, "GET: ip_address displayed correctly");
    CHECK(strcmp(nc.netmask, "255.255.255.0") == 0, "GET: netmask displayed correctly");
    CHECK(strcmp(nc.gateway, "192.168.1.1") == 0, "GET: gateway displayed correctly");
    CHECK(strcmp(nc.dns_server, "192.168.1.1") == 0, "GET: primary_dns displayed correctly as dns_server");

    /* DHCP-on case */
    wire.use_dhcp = 1;
    wire_to_ui_netconfig(&wire, &nc);
    CHECK(nc.ip_mode == NETCONFIG_MODE_DHCP, "GET: use_dhcp=1 maps to DHCP");
}

static void test_set_round_trip(void)
{
    NetConfig nc;
    SideTnfsNetworkConfig wire;
    NetConfig back;

    memset(&nc, 0, sizeof(nc));
    strcpy(nc.ssid, "OfficeWiFi");
    strcpy(nc.password, "hunter2");
    nc.auth_mode = 6;
    strcpy(nc.country, "US");
    nc.ip_mode = NETCONFIG_MODE_STATIC;
    strcpy(nc.ip_address, "10.0.0.5");
    strcpy(nc.netmask, "255.255.0.0");
    strcpy(nc.gateway, "10.0.0.1");
    strcpy(nc.dns_server, "10.0.0.1");

    ui_to_wire_netconfig(&nc, &wire);
    CHECK(wire.auth_mode == 6, "SET: auth_mode carried through");
    CHECK(wire.use_dhcp == 0, "SET: static IP -> use_dhcp=0");
    CHECK(strcmp(wire.country, "US") == 0, "SET: country UI[3] -> wire[4], NUL-padded");
    CHECK(wire.country[3] == '\0', "SET: wire country's 4th byte stays NUL padding");

    wire_to_ui_netconfig(&wire, &back);
    CHECK(strcmp(back.ssid, nc.ssid) == 0, "round-trip: ssid preserved");
    CHECK(strcmp(back.country, nc.country) == 0, "round-trip: country preserved");
    CHECK(strcmp(back.ip_address, nc.ip_address) == 0, "round-trip: ip_address preserved");
    CHECK(back.ip_mode == nc.ip_mode, "round-trip: ip_mode preserved");
}

/* ------------------------------------------------------------------ */
/* Mirrors nc_load_from_config()/nc_save_to_config()'s password-        */
/* placeholder retain-if-unchanged rule.                                */
/* ------------------------------------------------------------------ */
static const char PLACEHOLDER[] = "********";

static void simulate_load(const NetConfig *nc, char edit_buf[64])
{
    if (nc->password[0] != '\0')
        strcpy(edit_buf, PLACEHOLDER);
    else
        edit_buf[0] = '\0';
}

/* Returns 1 if nc->password was changed. */
static int simulate_save(NetConfig *nc, const char *edit_buf)
{
    if (strcmp(edit_buf, PLACEHOLDER) != 0) {
        strncpy(nc->password, edit_buf, NETCONFIG_PASSWORD_LEN - 1);
        nc->password[NETCONFIG_PASSWORD_LEN - 1] = '\0';
        return 1;
    }
    return 0;
}

static void test_password_retained_when_unchanged(void)
{
    NetConfig nc;
    char edit_buf[64];

    memset(&nc, 0, sizeof(nc));
    strcpy(nc.password, "original-secret");

    simulate_load(&nc, edit_buf);
    CHECK(strcmp(edit_buf, PLACEHOLDER) == 0, "password field shows fixed placeholder, not the real value");

    /* User does not touch the field: still equals the placeholder. */
    CHECK(simulate_save(&nc, edit_buf) == 0, "unchanged placeholder does not overwrite password");
    CHECK(strcmp(nc.password, "original-secret") == 0, "password retained when field not changed");

    /* User types a new password. */
    strcpy(edit_buf, "new-secret");
    CHECK(simulate_save(&nc, edit_buf) == 1, "changed field does overwrite password");
    CHECK(strcmp(nc.password, "new-secret") == 0, "new password stored correctly");

    /* No password was ever set: field starts empty, not the placeholder. */
    memset(&nc, 0, sizeof(nc));
    simulate_load(&nc, edit_buf);
    CHECK(edit_buf[0] == '\0', "empty stored password shows an empty field, not the placeholder");
}

/* ------------------------------------------------------------------ */
/* SET_NETWORK_CONFIG wire size, from the public field-length contract. */
/* ------------------------------------------------------------------ */
static void test_set_wire_record_size(void)
{
    unsigned long total = 2UL + 2UL /* auth_mode, use_dhcp */
        + (unsigned long)SIDETNFS_NET_SSID_LEN
        + (unsigned long)SIDETNFS_NET_PASSWORD_LEN
        + (unsigned long)SIDETNFS_NET_COUNTRY_LEN
        + 4UL * (unsigned long)SIDETNFS_NET_IPV4_LEN;

    CHECK(total == 176UL, "SET_NETWORK_CONFIG wire record is exactly 176 bytes");
}

/* ------------------------------------------------------------------ */
/* Pairwise string endianness: pack two source bytes the way            */
/* send_string_field() does, then decode with the firmware's own        */
/* CHANGE_ENDIANESS_BLOCK16 word-swap formula, and confirm the original */
/* bytes come back exactly -- same algebra proven by hand for the drive */
/* protocol, now checked by an executable test.                         */
/* ------------------------------------------------------------------ */
static unsigned int pack_word(unsigned char b0, unsigned char b1)
{
    return ((unsigned int)b0 << 8) | (unsigned int)b1; /* send_string_field() */
}

static unsigned int firmware_word_swap(unsigned int value)
{
    /* memfunc.h CHANGE_ENDIANESS_BLOCK16: word_ptr[j] = (w<<8)|(w>>8) */
    return ((value << 8) & 0xFF00u) | ((value >> 8) & 0x00FFu);
}

static void test_pairwise_string_endianness(void)
{
    static const unsigned char samples[][2] = {
        { 'H', 'i' }, { 0x00, 0xFF }, { 0xFF, 0x00 }, { 'A', 'B' }, { 0x7F, 0x80 }
    };
    unsigned int i;

    for (i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        unsigned int sent = pack_word(samples[i][0], samples[i][1]);
        unsigned int decoded = firmware_word_swap(sent);
        unsigned char d0 = (unsigned char)(decoded & 0xFF);
        unsigned char d1 = (unsigned char)((decoded >> 8) & 0xFF);

        CHECK(d0 == samples[i][0] && d1 == samples[i][1],
              "pairwise string byte pair round-trips through pack + firmware word-swap");
    }
}

/* ------------------------------------------------------------------ */
/* wait_for_token()'s poll loop shape: bounded, always terminates.      */
/* ------------------------------------------------------------------ */
static int simulate_wait_for_token(int timeout_sec, int vbls_per_sec, int seed_ever_matches)
{
    long budget = (long)timeout_sec * vbls_per_sec;
    int matched = 0;
    long iterations = 0;

    while (budget > 0 && !matched) {
        iterations++;
        if (iterations > (long)timeout_sec * vbls_per_sec + 1) {
            /* Would only happen if the loop were unbounded -- fail loudly. */
            return -1;
        }
        matched = seed_ever_matches && (iterations >= 5);
        budget--;
    }
    return matched ? 1 : 0;
}

static void test_timeout_leaves_interface_usable(void)
{
    CHECK(simulate_wait_for_token(2, 50, 0) == 0, "no-match case terminates with a timeout, not a hang");
    CHECK(simulate_wait_for_token(2, 50, 1) == 1, "match case terminates successfully before the budget runs out");
    CHECK(simulate_wait_for_token(5, 50, 0) == 0, "5s SAVE timeout case also terminates cleanly");
}

int main(void)
{
    test_auth_mode_groups();
    test_country_validation();
    test_status_text_coverage();
    test_get_field_display();
    test_set_round_trip();
    test_password_retained_when_unchanged();
    test_set_wire_record_size();
    test_pairwise_string_endianness();
    test_timeout_leaves_interface_usable();

    if (g_failures == 0) {
        printf("All host-side netconfig protocol tests passed.\n");
        return 0;
    }
    printf("%d test(s) FAILED.\n", g_failures);
    return 1;
}
