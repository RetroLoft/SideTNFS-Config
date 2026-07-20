#include <string.h>
#include "netconfig.h"

void netconfig_init_defaults(NetConfig *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    cfg->auth_mode = 3; /* WPA2/AES -- reasonable default before any GET_NETWORK_CONFIG */
    strncpy(cfg->country, "XX", NETCONFIG_COUNTRY_LEN - 1);

    cfg->ip_mode = NETCONFIG_MODE_DHCP;
    strncpy(cfg->ip_address, "192.168.178.50", NETCONFIG_IPV4_LEN - 1);
    strncpy(cfg->netmask,    "255.255.255.0",  NETCONFIG_IPV4_LEN - 1);
    strncpy(cfg->gateway,    "192.168.178.1",  NETCONFIG_IPV4_LEN - 1);
    strncpy(cfg->dns_server, "192.168.178.1",  NETCONFIG_IPV4_LEN - 1);
}
