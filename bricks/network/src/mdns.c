/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * mdns.c — mDNS / DNS-SD support for zego/network.
 *
 * Compiled only when CONFIG_ZEGO_NETWORK_MDNS=y.
 *
 * What this file does
 * -------------------
 * 1. When CONFIG_ZEGO_NETWORK_MDNS_HTTP_PORT > 0 it places a compile-time
 *    _http._tcp.local DNS-SD service record in the .dns_sd linker section.
 *    The Zephyr mDNS responder reads this section at query time — no runtime
 *    registration call is required.
 *
 * 2. Logs the active hostname at APPLICATION priority 6 (one step after the
 *    network module at priority 5) so the operator sees the .local name in
 *    the boot banner before any Wi-Fi link comes up.
 *
 * What the Kconfig symbols do (set automatically by ZEGO_NETWORK_MDNS)
 * ---------------------------------------------------------------------
 *   CONFIG_NET_HOSTNAME_ENABLE=y  — enables net_hostname_init(); sets the
 *                                   hostname to CONFIG_NET_HOSTNAME at boot.
 *   CONFIG_MDNS_RESPONDER=y       — Zephyr mDNS responder joins 224.0.0.251
 *                                   on every UP interface; answers A / PTR
 *                                   queries for <hostname>.local.
 *   CONFIG_DNS_SD=y               — enables DNS Service Discovery records;
 *                                   MDNS_RESPONDER_DNS_SD defaults to y when
 *                                   DNS_SD is enabled.
 *
 * Hostname
 * --------
 *   Default (from Kconfig.defaults): "zego-device" → zego-device.local
 *   Override in prj.conf:  CONFIG_NET_HOSTNAME="myapp"
 *
 * Per-mode reliability
 * --------------------
 *   SoftAP / P2P_GO  — mDNS multicast stays on the 192.168.7.0/24 segment;
 *                       reliable as long as a client is connected.
 *   STA              — depends on the router forwarding mDNS multicast;
 *                       home routers usually do, enterprise APs sometimes block it.
 *   P2P_GC           — advertised, but the phone acting as GO rarely forwards
 *                       mDNS multicast; direct IP access is the reliable fallback.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#if CONFIG_ZEGO_NETWORK_MDNS_HTTP_PORT > 0
#include <zephyr/net/dns_sd.h>
#include <zephyr/sys/byteorder.h>

static const uint16_t http_port = sys_cpu_to_be16(CONFIG_ZEGO_NETWORK_MDNS_HTTP_PORT);

DNS_SD_REGISTER_SERVICE(zego_http, CONFIG_NET_HOSTNAME, "_http", "_tcp", "local", DNS_SD_EMPTY_TXT,
			&http_port);
#endif /* CONFIG_ZEGO_NETWORK_MDNS_HTTP_PORT > 0 */

LOG_MODULE_REGISTER(zego_mdns, CONFIG_ZEGO_NETWORK_LOG_LEVEL);

static int mdns_log_init(void)
{
	LOG_INF("mDNS: device reachable as %s.local", CONFIG_NET_HOSTNAME);
#if CONFIG_ZEGO_NETWORK_MDNS_HTTP_PORT > 0
	LOG_INF("mDNS: DNS-SD _http._tcp.local port=%d", CONFIG_ZEGO_NETWORK_MDNS_HTTP_PORT);
#endif
	return 0;
}

SYS_INIT(mdns_log_init, APPLICATION, 6);
