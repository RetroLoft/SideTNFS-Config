/*
 * Host-side logic tests for the status window's Clock section.
 *
 * Same arrangement as tests/test_netconfig_protocol.c: this is NOT part
 * of the cross-compiled SIDETNFS.PRG build. dialog.c cannot be linked on
 * the development host (it calls GEM/AES and dereferences literal ROM3
 * bus addresses), so the pure functions under test are mirrored here
 * verbatim from dialog.c and kept in sync by inspection -- dialog.c's
 * copies have internal linkage.
 *
 * Build/run with:
 *   cc -Wall -Wextra -o /tmp/test_clock tests/test_clock_status.c \
 *       -I include && /tmp/test_clock
 *
 * Covered:
 *   - gemdos_decode_date(): bit layout and the 1980 epoch;
 *   - gemdos_decode_time(): bit layout and the two-second seconds step;
 *   - gemdos_format_clock(): full vs. short form, and the fixed width
 *     that lets a new value fully overwrite the previous one;
 *   - the right-alignment arithmetic sw_dialog_init() uses, at several
 *     window widths and character cell sizes;
 *   - rtc_sync_text(): every known state plus an unknown value;
 *   - format_utc_offset(): available versus unavailable/invalid.
 *
 * Not covered here (needs real hardware or the AES): the ROM3 reads in
 * sidetnfs_probe_get_rtc_sync_state() itself, and the evnt_multi() timer
 * loop.
 */
#include <stdio.h>
#include <string.h>

#include "sidetnfs_probe.h" /* SIDETNFS_RTC_SYNC_* */

static int g_checks;
static int g_failures;

#define CHECK(cond, msg)                                             \
    do {                                                              \
        g_checks++;                                                   \
        if (!(cond)) {                                                \
            g_failures++;                                             \
            printf("  FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);  \
        }                                                             \
    } while (0)

/* ================================================================== */
/* Mirrors of dialog.c (verbatim)                                      */
/* ================================================================== */

#define SW_CLOCK_TEXT_BUF    24
#define SW_CLOCK_FULL_CHARS  19
#define SW_CLOCK_SHORT_CHARS 8

static void gemdos_decode_date(unsigned short packed, int *year, int *month, int *day)
{
    *day   = (int)(packed & 0x1FU);
    *month = (int)((packed >> 5) & 0x0FU);
    *year  = 1980 + (int)((packed >> 9) & 0x7FU);
}

static void gemdos_decode_time(unsigned short packed, int *hour, int *minute, int *second)
{
    *second = (int)((packed & 0x1FU) * 2U);
    *minute = (int)((packed >> 5) & 0x3FU);
    *hour   = (int)((packed >> 11) & 0x1FU);
}

static void gemdos_format_clock(unsigned short date, unsigned short time, int field_chars, char *out)
{
    int year, month, day, hour, minute, second;

    if (field_chars >= SW_CLOCK_FULL_CHARS) {
        gemdos_decode_date(date, &year, &month, &day);
        gemdos_decode_time(time, &hour, &minute, &second);
        sprintf(out, "%02d-%02d-%04d %02d:%02d:%02d", day, month, year, hour, minute, second);
    } else if (field_chars >= SW_CLOCK_SHORT_CHARS) {
        gemdos_decode_time(time, &hour, &minute, &second);
        sprintf(out, "%02d:%02d:%02d", hour, minute, second);
    } else {
        out[0] = '\0';
    }
}

static const char *rtc_sync_text(int state)
{
    switch (state) {
    case SIDETNFS_RTC_SYNC_SYNCED:   return "Synchronized";
    case SIDETNFS_RTC_SYNC_DISABLED: return "Disabled";
    case SIDETNFS_RTC_SYNC_NOT_SYNCED:
    default:                          return "Not synchronized";
    }
}

static int format_utc_offset(const char *offset, char *out)
{
    const char *p = offset;
    int sign = 1;
    int value = 0;
    int digits = 0;

    if (p == 0 || *p == '\0')
        return 0;

    if (*p == '+') {
        p++;
    } else if (*p == '-') {
        sign = -1;
        p++;
    }

    while (*p >= '0' && *p <= '9') {
        value = value * 10 + (*p - '0');
        p++;
        digits++;
        if (digits > 2)
            return 0;
    }
    if (digits == 0 || *p != '\0')
        return 0;

    if (sign > 0 ? (value > 14) : (value > 12))
        return 0;

    sprintf(out, "UTC%c%02d:00", (sign < 0) ? '-' : '+', value);
    return 1;
}

/* Mirrors sw_dialog_init()'s clock-field arithmetic: the time sits in the
 * shared value column, so what it may occupy is whatever the line has
 * left after the label. */
static int clock_field_chars(int line_cols, int label_cols)
{
    int avail_chars = line_cols - label_cols;

    if (avail_chars >= SW_CLOCK_FULL_CHARS)
        return SW_CLOCK_FULL_CHARS;
    if (avail_chars >= SW_CLOCK_SHORT_CHARS)
        return SW_CLOCK_SHORT_CHARS;
    return 0;
}

static int clock_field_x(int cw, int xl, int label_cols)
{
    return xl + label_cols * cw;
}

/* ================================================================== */
/* Tests                                                              */
/* ================================================================== */

/* Packs a date the way GEMDOS does, so the test states the intent and
 * the mirror does the decoding. */
static unsigned short pack_date(int year, int month, int day)
{
    return (unsigned short)((((year - 1980) & 0x7F) << 9) | ((month & 0x0F) << 5) | (day & 0x1F));
}

static unsigned short pack_time(int hour, int minute, int second)
{
    return (unsigned short)(((hour & 0x1F) << 11) | ((minute & 0x3F) << 5) | ((second / 2) & 0x1F));
}

static void test_date_decoding(void)
{
    int y, m, d;

    printf("Test 1: GEMDOS date decoding\n");

    gemdos_decode_date(pack_date(2026, 7, 27), &y, &m, &d);
    CHECK(y == 2026 && m == 7 && d == 27, "27-07-2026 round-trips");

    gemdos_decode_date(pack_date(1980, 1, 1), &y, &m, &d);
    CHECK(y == 1980 && m == 1 && d == 1, "the epoch itself is 01-01-1980, not year 0");

    gemdos_decode_date(pack_date(2107, 12, 31), &y, &m, &d);
    CHECK(y == 2107 && m == 12 && d == 31, "the highest representable date (1980+127)");

    /* A field must not bleed into its neighbour: month 12 sets bit 8,
     * which is adjacent to the year field. */
    gemdos_decode_date(pack_date(2000, 12, 31), &y, &m, &d);
    CHECK(y == 2000 && m == 12 && d == 31, "month 12 does not leak into the year field");

    gemdos_decode_date(0x0000, &y, &m, &d);
    CHECK(y == 1980 && m == 0 && d == 0, "an all-zero date decodes to the epoch with zero month/day");
}

static void test_time_decoding(void)
{
    int h, mi, s;

    printf("Test 2: GEMDOS time decoding, including the two-second step\n");

    gemdos_decode_time(pack_time(16, 45, 32), &h, &mi, &s);
    CHECK(h == 16 && mi == 45 && s == 32, "16:45:32 round-trips");

    gemdos_decode_time(pack_time(0, 0, 0), &h, &mi, &s);
    CHECK(h == 0 && mi == 0 && s == 0, "midnight");

    gemdos_decode_time(pack_time(23, 59, 58), &h, &mi, &s);
    CHECK(h == 23 && mi == 59 && s == 58, "the last representable second of the day");

    /* Seconds are stored halved, so the raw field counts 0..29 while the
     * displayed value counts 0..58 in steps of two. */
    gemdos_decode_time(0x0000 | 29, &h, &mi, &s);
    CHECK(s == 58, "raw seconds field 29 means 58 seconds, not 29");
    gemdos_decode_time(0x0000 | 1, &h, &mi, &s);
    CHECK(s == 2, "raw seconds field 1 means 2 seconds");

    /* An odd second is not representable: GEMDOS rounds it down, and the
     * display must show the even value rather than invent an odd one. */
    gemdos_decode_time(pack_time(12, 0, 33), &h, &mi, &s);
    CHECK(s == 32, "an odd second is stored rounded down and shown as even");

    gemdos_decode_time(pack_time(23, 59, 0), &h, &mi, &s);
    CHECK(h == 23 && mi == 59, "minutes do not leak into the hours field");
}

static void test_formatting(void)
{
    char out[SW_CLOCK_TEXT_BUF];

    printf("Test 3: clock formatting and fixed width\n");

    gemdos_format_clock(pack_date(2026, 7, 27), pack_time(16, 45, 32), SW_CLOCK_FULL_CHARS, out);
    CHECK(strcmp(out, "27-07-2026 16:45:32") == 0, "the full form matches the requested layout");
    CHECK(strlen(out) == (size_t)SW_CLOCK_FULL_CHARS, "the full form is exactly 19 characters");

    gemdos_format_clock(pack_date(2026, 7, 27), pack_time(16, 45, 32), SW_CLOCK_SHORT_CHARS, out);
    CHECK(strcmp(out, "16:45:32") == 0, "a narrow field falls back to time only");
    CHECK(strlen(out) == (size_t)SW_CLOCK_SHORT_CHARS, "the short form is exactly 8 characters");

    gemdos_format_clock(pack_date(2026, 7, 27), pack_time(16, 45, 32), 0, out);
    CHECK(out[0] == '\0', "no room at all yields no text rather than a clipped string");

    /* Zero padding is what makes an in-place redraw safe: single-digit
     * values must never shorten the string and leave stale characters. */
    gemdos_format_clock(pack_date(2026, 1, 2), pack_time(3, 4, 6), SW_CLOCK_FULL_CHARS, out);
    CHECK(strcmp(out, "02-01-2026 03:04:06") == 0, "single-digit fields are zero-padded");
    CHECK(strlen(out) == (size_t)SW_CLOCK_FULL_CHARS, "a single-digit time is still 19 characters");

    gemdos_format_clock(pack_date(2107, 12, 31), pack_time(23, 59, 58), SW_CLOCK_FULL_CHARS, out);
    CHECK(strlen(out) == (size_t)SW_CLOCK_FULL_CHARS, "the widest possible value is still 19 characters");
    CHECK(strlen(out) < SW_CLOCK_TEXT_BUF, "the widest value fits the buffer with room for the NUL");
}

/* Every value the clock can ever show must be the same length, otherwise
 * a shrinking string would leave part of the previous one on screen. */
static void test_overwrite_safety(void)
{
    char out[SW_CLOCK_TEXT_BUF];
    size_t len_full = 0;
    int h, mi, day;

    printf("Test 4: every rendered value has the same width\n");

    for (day = 1; day <= 31; day++) {
        gemdos_format_clock(pack_date(2026, 7, day), pack_time(1, 2, 4), SW_CLOCK_FULL_CHARS, out);
        if (len_full == 0)
            len_full = strlen(out);
        if (strlen(out) != len_full)
            break;
    }
    CHECK(day > 31, "the full form keeps one width across every day of the month");

    for (h = 0; h <= 23; h++) {
        for (mi = 0; mi <= 59; mi++) {
            gemdos_format_clock(pack_date(2026, 7, 27), pack_time(h, mi, 0), SW_CLOCK_SHORT_CHARS, out);
            if (strlen(out) != (size_t)SW_CLOCK_SHORT_CHARS)
                goto short_mismatch;
        }
    }
short_mismatch:
    CHECK(h > 23, "the short form keeps one width across every hour and minute");
}

static void test_clock_placement(void)
{
    int chars, x;

    printf("Test 5: clock placement in the shared value column\n");

    /* The shipping geometry: 40-column lines, 9-column labels. */
    chars = clock_field_chars(40, 9);
    CHECK(chars == SW_CLOCK_FULL_CHARS, "the shipping line width fits the full form");
    CHECK(9 + chars <= 40, "the label plus the time never exceed the line");

    /* The x position is in real pixels, but derived only from the
     * character cell, so it tracks the resolution instead of being fixed. */
    x = clock_field_x(8, 2 * 8, 9);
    CHECK(x == 2 * 8 + 9 * 8, "at 8-pixel cells the time starts in the value column");
    x = clock_field_x(16, 2 * 16, 9);
    CHECK(x == 2 * 16 + 9 * 16, "at 16-pixel cells it scales with the cell, not with a constant");

    /* The time must line up with the values on the lines around it. */
    CHECK(clock_field_x(8, 2 * 8, 9) == 2 * 8 + 9 * 8,
          "the time starts in exactly the same column as the SSID and IP values");

    /* Narrower line budgets fall back rather than overflowing. */
    chars = clock_field_chars(28, 9);
    CHECK(chars == SW_CLOCK_FULL_CHARS, "28 columns is the narrowest that still fits the full form");
    chars = clock_field_chars(27, 9);
    CHECK(chars == SW_CLOCK_SHORT_CHARS, "one column narrower drops to the short form");
    chars = clock_field_chars(17, 9);
    CHECK(chars == SW_CLOCK_SHORT_CHARS, "17 columns still fits the short form");
    chars = clock_field_chars(16, 9);
    CHECK(chars == 0, "below that the clock is dropped rather than drawn over its neighbour");
}

static void test_ntp_status(void)
{
    printf("Test 6: NTP status text\n");

    CHECK(strcmp(rtc_sync_text(SIDETNFS_RTC_SYNC_SYNCED), "Synchronized") == 0,
          "an explicitly synchronised Pico reads Synchronized");
    CHECK(strcmp(rtc_sync_text(SIDETNFS_RTC_SYNC_NOT_SYNCED), "Not synchronized") == 0,
          "enabled but never synchronised reads Not synchronized");
    CHECK(strcmp(rtc_sync_text(SIDETNFS_RTC_SYNC_DISABLED), "Disabled") == 0,
          "RTC switched off reads Disabled");

    /* An unknown state must never be optimistic. */
    CHECK(strcmp(rtc_sync_text(99), "Not synchronized") == 0,
          "an unknown state falls back to Not synchronized, never to Synchronized");
    CHECK(strcmp(rtc_sync_text(-1), "Not synchronized") == 0,
          "a negative state also falls back to Not synchronized");
}

static void test_timezone(void)
{
    char out[32];

    printf("Test 7: timezone available versus unavailable\n");

    CHECK(format_utc_offset("+1", out) && strcmp(out, "UTC+01:00") == 0, "\"+1\" reads UTC+01:00");
    CHECK(format_utc_offset("0", out) && strcmp(out, "UTC+00:00") == 0, "\"0\" reads UTC+00:00");
    CHECK(format_utc_offset("+0", out) && strcmp(out, "UTC+00:00") == 0, "\"+0\" reads UTC+00:00");
    CHECK(format_utc_offset("-5", out) && strcmp(out, "UTC-05:00") == 0, "\"-5\" reads UTC-05:00");
    CHECK(format_utc_offset("+14", out) && strcmp(out, "UTC+14:00") == 0, "the highest accepted offset");
    CHECK(format_utc_offset("-12", out) && strcmp(out, "UTC-12:00") == 0, "the lowest accepted offset");

    /* Anything the firmware would not have accepted must fall back to
     * "-" rather than be rendered as if it were a real zone. */
    CHECK(!format_utc_offset("", out), "an empty offset is unavailable");
    CHECK(!format_utc_offset(0, out), "a null offset is unavailable");
    CHECK(!format_utc_offset("+15", out), "an out-of-range positive offset is rejected");
    CHECK(!format_utc_offset("-13", out), "an out-of-range negative offset is rejected");
    CHECK(!format_utc_offset("+", out), "a sign with no digits is rejected");
    CHECK(!format_utc_offset("abc", out), "a non-numeric offset is rejected");
    CHECK(!format_utc_offset("+1x", out), "trailing rubbish is rejected");
    CHECK(!format_utc_offset("+100", out), "too many digits are rejected");

    /* The rendered form never claims a zone name, only the offset the
     * firmware actually applies. */
    format_utc_offset("+1", out);
    CHECK(strstr(out, "Europe") == 0 && strstr(out, "/") == 0,
          "the display is a plain offset, never a zone name");
}

/* Mirrors status_refresh_network()'s Status-line decision. Kept as a
 * separate function here so every combination can be exercised without
 * the AES or a real ROM3 read. */
enum { M_NETLOAD_OK = 0, M_NETLOAD_BAD_STATUS, M_NETLOAD_UNAVAILABLE };

static const char *network_status_text(int load_state, int link_state, const char *ssid,
                                        const char *fw_status_text)
{
    switch (load_state) {
    case M_NETLOAD_OK:
        if (link_state == SIDETNFS_NET_LINK_CONNECTED)
            return "Connected";
        if (ssid[0] == '\0')
            return "Not configured";
        return "Not connected";
    case M_NETLOAD_BAD_STATUS:
        return fw_status_text;
    case M_NETLOAD_UNAVAILABLE:
    default:
        return "Unavailable";
    }
}

static void test_network_status(void)
{
    printf("Test 8: network status line\n");

    CHECK(strcmp(network_status_text(M_NETLOAD_OK, SIDETNFS_NET_LINK_CONNECTED, "MyAP", "x"),
                 "Connected") == 0,
          "WiFi up at boot reads Connected");
    CHECK(strcmp(network_status_text(M_NETLOAD_OK, SIDETNFS_NET_LINK_NOT_CONNECTED, "MyAP", "x"),
                 "Not connected") == 0,
          "an SSID that never associated reads Not connected");
    CHECK(strcmp(network_status_text(M_NETLOAD_OK, SIDETNFS_NET_LINK_NOT_CONNECTED, "", "x"),
                 "Not configured") == 0,
          "no SSID stored reads Not configured, not Not connected");

    /* A stored SSID alone must never be enough to claim a link. */
    CHECK(strcmp(network_status_text(M_NETLOAD_OK, SIDETNFS_NET_LINK_NOT_CONNECTED, "MyAP", "x"),
                 "Connected") != 0,
          "Connected is never inferred from a configured SSID");

    /* Without a firmware answer there is nothing to say about the link,
     * whatever a stray ROM3 read might return. */
    CHECK(strcmp(network_status_text(M_NETLOAD_UNAVAILABLE, SIDETNFS_NET_LINK_CONNECTED, "MyAP", "x"),
                 "Unavailable") == 0,
          "no firmware answer reads Unavailable, never Connected");
    CHECK(strcmp(network_status_text(M_NETLOAD_BAD_STATUS, SIDETNFS_NET_LINK_CONNECTED, "MyAP", "Invalid SSID"),
                 "Invalid SSID") == 0,
          "a firmware error status is reported as-is, never overridden by the link state");
}

/* ---- layout mirrors (dialog.c, verbatim) ---- */

#define SW_LINE_COLS        40
#define SW_LABEL_COLS        9
#define SW_DRV_NAME_COL      4
#define SW_DRV_BACKEND_COLS  8
#define SW_DRV_NAME_COLS     (SW_LINE_COLS - SW_DRV_NAME_COL - SW_DRV_BACKEND_COLS - 1)
#define SW_LINE_BUF         48

#define M_NETCONFIG_MODE_DHCP 0

static void sw_set_labelled_line(char *dst, const char *label, const char *value)
{
    sprintf(dst, "%-*s%.*s", SW_LABEL_COLS, label, SW_LINE_COLS - SW_LABEL_COLS, value);
    dst[SW_LINE_BUF - 1] = '\0';
}

static void sw_format_ip_value(int ip_mode, const char *ip_address, char *out)
{
    if (ip_mode == M_NETCONFIG_MODE_DHCP)
        strcpy(out, "Set by DHCP server");
    else if (ip_address[0] != '\0')
        sprintf(out, "%.28s", ip_address);
    else
        strcpy(out, "-");
}

static void sw_format_drive_row(char *dst, char letter, const char *name, const char *backend)
{
    if (name == 0 || name[0] == '\0')
        name = "(unnamed)";

    sprintf(dst, "%c:  %-*.*s %*s", letter,
            SW_DRV_NAME_COLS, SW_DRV_NAME_COLS, name,
            SW_DRV_BACKEND_COLS, backend);
    dst[SW_LINE_BUF - 1] = '\0';
}

static void sw_format_drive_header(char *dst, int total)
{
    char count_text[8];

    sprintf(count_text, "%d", total);
    sprintf(dst, "%-*s%*s",
            SW_LINE_COLS - (int)strlen(count_text), "Active drives:",
            (int)strlen(count_text), count_text);
    dst[SW_LINE_BUF - 1] = '\0';
}

/* Mirrors sw_collect_active_drives(): ENABLED ordinary slots plus the
 * always-active SETTINGS disk, sorted ascending by letter. */
#define M_MAX_DRIVES 8
typedef struct { char letter; const char *name; const char *backend; } MRow;
typedef struct { int enabled; char letter; const char *name; int is_tnfs; } MSlot;

static int m_collect(const MSlot *slots, int nslots, char settings_letter, MRow *out, int max)
{
    int i, j, count = 0;
    MRow all[M_MAX_DRIVES + 1];

    for (i = 0; i < nslots; i++) {
        if (!slots[i].enabled)
            continue;
        all[count].letter  = slots[i].letter;
        all[count].name    = slots[i].name;
        all[count].backend = slots[i].is_tnfs ? "TNFS" : "SD";
        count++;
    }
    all[count].letter  = settings_letter;
    all[count].name    = "Settings";
    all[count].backend = "SETTINGS";
    count++;

    for (i = 1; i < count; i++) {
        MRow key = all[i];
        for (j = i - 1; j >= 0 && all[j].letter > key.letter; j--)
            all[j + 1] = all[j];
        all[j + 1] = key;
    }
    for (i = 0; i < count && i < max; i++)
        out[i] = all[i];
    return count;
}

static void test_line_layout(void)
{
    char line[SW_LINE_BUF];

    printf("Test 9: labelled lines share one value column\n");

    sw_set_labelled_line(line, "Network:", "Connected");
    CHECK(strcmp(line, "Network: Connected") == 0, "Network line matches the agreed layout");
    sw_set_labelled_line(line, "SSID:", "Zolder");
    CHECK(strncmp(line, "SSID:    Zolder", 15) == 0, "SSID value starts in the same column");
    sw_set_labelled_line(line, "IP:", "192.168.18.22");
    CHECK(strncmp(line, "IP:      192.168.18.22", 22) == 0, "IP value starts in the same column");
    sw_set_labelled_line(line, "IP:", "Set by DHCP server");
    CHECK(strncmp(line, "IP:      Set by DHCP server", 27) == 0, "the DHCP text starts there too");

    /* A very long SSID must be cut at the line width, never run past the
     * object into whatever is drawn beside it. */
    sw_set_labelled_line(line, "SSID:", "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ABCDEFGHIJ");
    CHECK(strlen(line) == SW_LINE_COLS, "an over-long SSID is truncated to the line width");
    CHECK(strcmp(line, "SSID:    ABCDEFGHIJKLMNOPQRSTUVWXYZ01234") == 0,
          "the truncated SSID keeps its leading characters");

    /* The longest NTP line still has to fit the value budget. */
    {
        char value[SW_LINE_BUF];
        sprintf(value, "%-16s  TZ: %s", "Not synchronized", "UTC+01:00");
        CHECK(strlen(value) <= (size_t)(SW_LINE_COLS - SW_LABEL_COLS),
              "the longest NTP+TZ value fits the line without truncation");
        sw_set_labelled_line(line, "NTP:", value);
        CHECK(strcmp(line, "NTP:     Not synchronized  TZ: UTC+01:00") == 0,
              "the longest NTP line renders in full");
        CHECK(strlen(line) <= SW_LINE_COLS, "and still fits the line width");
    }
    {
        char value[SW_LINE_BUF];
        sprintf(value, "%-16s  TZ: %s", "Synchronized", "UTC+01:00");
        sw_set_labelled_line(line, "NTP:", value);
        CHECK(strcmp(line, "NTP:     Synchronized      TZ: UTC+01:00") == 0,
              "a shorter status keeps TZ in the same column");
    }
    {
        char value[SW_LINE_BUF];
        sprintf(value, "%-16s  TZ: %s", "Disabled", "-");
        sw_set_labelled_line(line, "NTP:", value);
        CHECK(strstr(line, "TZ: -") != 0, "a missing timezone renders as TZ: -");
    }
}

static void test_ip_line(void)
{
    char value[40];

    printf("Test 10: IP line\n");

    /* Static mode: the stored address is the one the Pico uses. */
    sw_format_ip_value(1, "192.168.18.22", value);
    CHECK(strcmp(value, "192.168.18.22") == 0, "a static address is shown as-is");

    /* DHCP mode: the assigned address is not available to this program,
     * so it must not fall back to the unused stored static field, and it
     * must not present an address it does not have. */
    sw_format_ip_value(M_NETCONFIG_MODE_DHCP, "192.168.10.50", value);
    CHECK(strcmp(value, "Set by DHCP server") == 0,
          "DHCP names who assigns the address instead of showing the stale static field");
    CHECK(strstr(value, "192.168") == 0, "the stored static field never leaks into the DHCP text");
    sw_format_ip_value(M_NETCONFIG_MODE_DHCP, "", value);
    CHECK(strcmp(value, "Set by DHCP server") == 0, "DHCP reads the same with nothing stored");

    sw_format_ip_value(1, "", value);
    CHECK(strcmp(value, "-") == 0, "static mode with no address stored shows -");

    /* Both forms have to fit the value column without truncation. */
    sw_format_ip_value(M_NETCONFIG_MODE_DHCP, "", value);
    CHECK(strlen(value) <= (size_t)(SW_LINE_COLS - SW_LABEL_COLS),
          "the DHCP text fits the value column");
    sw_format_ip_value(1, "255.255.255.255", value);
    CHECK(strlen(value) <= (size_t)(SW_LINE_COLS - SW_LABEL_COLS),
          "the longest IPv4 address fits the value column");
}

static void test_drive_rows(void)
{
    char line[SW_LINE_BUF];

    printf("Test 11: drive rows and truncation\n");

    sw_format_drive_row(line, 'N', "Retroloft", "TNFS");
    CHECK(strlen(line) == SW_LINE_COLS, "a drive row is exactly the line width");
    CHECK(strncmp(line, "N:  Retroloft", 13) == 0, "letter and name start in the expected columns");
    CHECK(strcmp(line + SW_LINE_COLS - 4, "TNFS") == 0, "TNFS ends flush with the right margin");

    sw_format_drive_row(line, 'S', "Settings", "SETTINGS");
    CHECK(strcmp(line + SW_LINE_COLS - 8, "SETTINGS") == 0,
          "the longest backend name still ends flush right");
    CHECK(strlen(line) == SW_LINE_COLS, "and does not lengthen the row");

    sw_format_drive_row(line, 'P', "Harddisk 1", "SD");
    CHECK(strcmp(line + SW_LINE_COLS - 2, "SD") == 0, "SD ends flush right too");

    /* An over-long name must lose characters, never the backend. */
    sw_format_drive_row(line, 'Q', "A very very very long drive nickname indeed", "SETTINGS");
    CHECK(strlen(line) == SW_LINE_COLS, "an over-long name does not lengthen the row");
    CHECK(strcmp(line + SW_LINE_COLS - 8, "SETTINGS") == 0,
          "the backend stays fully visible when the name is truncated");
    CHECK(line[SW_DRV_NAME_COL + SW_DRV_NAME_COLS] == ' ',
          "a gap is kept between a truncated name and the backend");

    /* An empty name gets a readable fallback rather than blank space. */
    sw_format_drive_row(line, 'R', "", "SD");
    CHECK(strncmp(line, "R:  (unnamed)", 13) == 0, "an empty nickname falls back to (unnamed)");
    sw_format_drive_row(line, 'R', 0, "SD");
    CHECK(strncmp(line, "R:  (unnamed)", 13) == 0, "a null nickname does too");
}

static void test_drive_header_and_list(void)
{
    char line[SW_LINE_BUF];
    MRow rows[5];
    int total;
    MSlot slots[M_MAX_DRIVES];
    int i;

    printf("Test 12: header count and the active-drive list\n");

    sw_format_drive_header(line, 7);
    CHECK(strncmp(line, "Active drives:", 14) == 0, "the header reads exactly \"Active drives:\"");
    CHECK(strlen(line) == SW_LINE_COLS, "the header spans the whole line");
    CHECK(line[SW_LINE_COLS - 1] == '7', "the count sits flush against the right margin");
    sw_format_drive_header(line, 10);
    CHECK(strcmp(line + SW_LINE_COLS - 2, "10") == 0, "a two-digit count still ends flush right");
    CHECK(strlen(line) == SW_LINE_COLS, "and does not widen the line");

    /* 0 ordinary drives: the SETTINGS disk alone is still active. */
    for (i = 0; i < M_MAX_DRIVES; i++) { slots[i].enabled = 0; slots[i].letter = 0; slots[i].name = ""; slots[i].is_tnfs = 0; }
    total = m_collect(slots, M_MAX_DRIVES, 'S', rows, 5);
    CHECK(total == 1, "with no ordinary drives the count is 1, the SETTINGS disk");
    CHECK(rows[0].letter == 'S' && strcmp(rows[0].backend, "SETTINGS") == 0,
          "SETTINGS appears as an ordinary row in the list");

    /* 1 ordinary drive -> 2 rows total. */
    slots[0].enabled = 1; slots[0].letter = 'N'; slots[0].name = "Retroloft"; slots[0].is_tnfs = 1;
    total = m_collect(slots, M_MAX_DRIVES, 'S', rows, 5);
    CHECK(total == 2, "one ordinary drive plus SETTINGS is 2");
    CHECK(rows[0].letter == 'N' && rows[1].letter == 'S', "sorted ascending by letter");

    /* Non-contiguous letters, and a SETTINGS letter that sorts first. */
    for (i = 0; i < M_MAX_DRIVES; i++) slots[i].enabled = 0;
    slots[3].enabled = 1; slots[3].letter = 'U'; slots[3].name = "Far";  slots[3].is_tnfs = 1;
    slots[6].enabled = 1; slots[6].letter = 'N'; slots[6].name = "Near"; slots[6].is_tnfs = 0;
    total = m_collect(slots, M_MAX_DRIVES, 'D', rows, 5);
    CHECK(total == 3, "non-contiguous letters are all counted");
    CHECK(rows[0].letter == 'D' && rows[1].letter == 'N' && rows[2].letter == 'U',
          "gaps in the letters do not disturb the ascending order");
    CHECK(strcmp(rows[0].backend, "SETTINGS") == 0,
          "a SETTINGS letter that sorts first is listed first, not pinned to the end");

    /* Exactly 5 -> no overflow line. */
    for (i = 0; i < M_MAX_DRIVES; i++) slots[i].enabled = 0;
    for (i = 0; i < 4; i++) { slots[i].enabled = 1; slots[i].letter = (char)('N' + i); slots[i].name = "x"; slots[i].is_tnfs = 1; }
    total = m_collect(slots, M_MAX_DRIVES, 'S', rows, 5);
    CHECK(total == 5, "four ordinary drives plus SETTINGS is exactly 5");
    CHECK(total <= 5, "five drives fit without an overflow line");

    /* 6 -> one over. */
    slots[4].enabled = 1; slots[4].letter = 'R'; slots[4].name = "x"; slots[4].is_tnfs = 0;
    total = m_collect(slots, M_MAX_DRIVES, 'S', rows, 5);
    CHECK(total == 6, "five ordinary drives plus SETTINGS is 6");
    CHECK(total - 5 == 1, "one drive beyond the five shown");
    sprintf(line, "(and %d more)", total - 5);
    CHECK(strcmp(line, "(and 1 more)") == 0, "the overflow line names the right number");

    /* 8 ordinary + SETTINGS = 9, the maximum. */
    for (i = 0; i < M_MAX_DRIVES; i++) { slots[i].enabled = 1; slots[i].letter = (char)('C' + i); slots[i].name = "x"; slots[i].is_tnfs = (i & 1); }
    total = m_collect(slots, M_MAX_DRIVES, 'S', rows, 5);
    CHECK(total == 9, "eight ordinary drives plus SETTINGS is 9");
    sprintf(line, "(and %d more)", total - 5);
    CHECK(strcmp(line, "(and 4 more)") == 0, "the overflow count is right at the maximum too");
    CHECK(rows[0].letter == 'C' && rows[4].letter == 'G',
          "the five shown are the five lowest letters, in order");

    /* A DISABLED slot is stored but never published, so never listed. */
    for (i = 0; i < M_MAX_DRIVES; i++) slots[i].enabled = 0;
    slots[0].enabled = 1; slots[0].letter = 'N'; slots[0].name = "on";  slots[0].is_tnfs = 1;
    slots[1].enabled = 0; slots[1].letter = 'O'; slots[1].name = "off"; slots[1].is_tnfs = 1;
    total = m_collect(slots, M_MAX_DRIVES, 'S', rows, 5);
    CHECK(total == 2, "a disabled slot is not counted as an active drive");
}

int main(void)
{
    test_date_decoding();
    test_time_decoding();
    test_formatting();
    test_overwrite_safety();
    test_clock_placement();
    test_ntp_status();
    test_timezone();
    test_network_status();
    test_line_layout();
    test_ip_line();
    test_drive_rows();
    test_drive_header_and_list();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
