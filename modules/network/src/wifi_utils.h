/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef WIFI_UTILS_H
#define WIFI_UTILS_H

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>

/**
 * @brief Run SoftAP mode: set regulatory domain, start DHCP server, enable AP.
 *
 * @return 0 on success, negative error code on failure.
 */
int wifi_run_softap_mode(void);

/**
 * @brief Start the DHCPv4 server on the Wi-Fi interface.
 *
 * Assigns the static gateway IP (CONFIG_NET_CONFIG_MY_IPV4_ADDR / 24) to the
 * Wi-Fi interface and starts the DHCP server with a pool at .2.  Safe to call
 * from both SoftAP and P2P GO mode.  Calling it a second time is a no-op.
 *
 * @return 0 on success, negative error code on failure.
 */
int wifi_setup_dhcp_server(void);

/**
 * @brief Print detailed Wi-Fi interface status to the log.
 *
 * @return 0 on success, negative error code on failure.
 */
int wifi_print_status(void);

/**
 * @brief Print DHCP-assigned IP address, netmask, and gateway to the log.
 *
 * @param iface Pointer to the network interface.
 * @param cb    Network management event callback containing DHCP info.
 */
void wifi_print_dhcp_ip(struct net_if *iface, struct net_mgmt_event_callback *cb);

/**
 * @brief Get the last connected SSID string.
 *
 * @return Pointer to the last SSID stored, or NULL if none.
 */
const char *wifi_utils_get_last_ssid(void);

/**
 * @brief Ensure default SoftAP credentials exist in persistent storage.
 *
 * Stores CONFIG_ZEGO_WIIF_SOFTAP_SSID / CONFIG_ZEGO_WIIF_SOFTAP_PASSWORD with WPA2-PSK
 * if they are not already present.
 *
 * @return 0 on success or if credentials already exist; negative errno otherwise.
 */
int wifi_utils_ensure_gateway_softap_credentials(void);

/**
 * @brief Request connection using stored Wi-Fi credentials.
 *
 * Triggers NET_REQUEST_WIFI_CONNECT_STORED so the station automatically
 * connects to previously stored networks.
 *
 * @return 0 on success, -EALREADY if already connecting, negative errno otherwise.
 */
int wifi_utils_auto_connect_stored(void);

/**
 * @brief Set Wi-Fi regulatory domain (from CONFIG_ZEGO_WIIF_SOFTAP_REG_DOMAIN).
 *
 * @return 0 on success, negative error code on failure.
 */
int wifi_set_reg_domain(void);

/**
 * @brief Set Wi-Fi channel for raw packet operations.
 *
 * @param channel Channel number to set.
 * @return 0 on success, negative error code on failure.
 */
int wifi_set_channel(int channel);

/**
 * @brief Set Wi-Fi mode.
 *
 * @param mode Mode value to set.
 * @return 0 on success, negative error code on failure.
 */
int wifi_set_mode(int mode);

/**
 * @brief Enable TX injection mode.
 *
 * @return 0 on success, negative error code on failure.
 */
int wifi_set_tx_injection_mode(void);

/**
 * @brief Start P2P_GO mode: create group and activate WPS PIN.
 *
 * Starts a 5-minute k_work_delayable timer; if it fires with no client, it
 * re-arms the WPS window (the group stays alive).
 *
 * @return 0 on success, negative error code on failure.
 */
int wifi_run_p2p_go_mode(void);

/**
 * @brief Cancel the SoftAP periodic reminder timer (call when first client connects).
 */
void wifi_softap_cancel_remind_timer(void);

/**
 * @brief Cancel the P2P_GO WPS-wait timer (call when first client connects).
 */
void wifi_p2p_go_cancel_wps_timer(void);

#endif /* WIFI_UTILS_H */
