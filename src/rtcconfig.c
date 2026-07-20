#include <string.h>
#include "rtcconfig.h"

void rtcconfig_init_defaults(RtcConfig *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    cfg->ntp_enabled = 1;
    strncpy(cfg->ntp_server, "pool.ntp.org", RTCCONFIG_NTP_SERVER_LEN - 1);
    strncpy(cfg->utc_offset, "+1", RTCCONFIG_UTC_OFFSET_LEN - 1);
}
