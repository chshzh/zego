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
 * @brief Start P2P_GO mode: create autonomous group, arm WPS PBC, start discovery.
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

/**
 * @brief Re-arm WPS PBC on the GO so a rebooted client can reconnect.
 *
 * Call this when a P2P_GO station disconnects.  PBC has a 120 s window; once
 * it expires the client cannot use `pbc --join` to rejoin.  This function
 * re-arms PBC and restarts P2P device discovery so the client can reconnect.
 */
void wifi_p2p_go_rearm_pbc(void);

/**
 * @brief Start P2P_CLIENT mode.
 *
 * If CONFIG_ZEGO_WIFI_P2P_CLIENT_TARGET_GO_MAC is set, automatically initiates
 * 'wifi p2p connect <MAC> pbc --join', retrying every 10 s until success.
 * Otherwise prints manual-connect instructions and returns.
 *
 * @return 0 on success, negative error code on failure.
 */
int wifi_run_p2p_client_mode(void);

/**
 * @brief Notify P2P_CLIENT auto-connect of a CONNECT_RESULT event.
 *
 * Call from the CONNECT_RESULT handler for P2P_CLIENT mode:
 *   - success=true  → cancel retry work
 *   - success=false → schedule next attempt in 10 s
 *
 * No-op if CONFIG_ZEGO_WIFI_P2P_CLIENT_TARGET_GO_MAC is empty (manual mode).
 *
 * @param success true if CONNECT_RESULT status == 0, false otherwise.
 */
void wifi_p2p_client_on_connect_result(bool success);

/**
 * @brief Notify P2P_CLIENT auto-connect of a DISCONNECT_RESULT event.
 *
 * Resets the connected + pending flags and schedules a reconnect attempt
 * in 5 s so the CLIENT re-joins the GO automatically after link loss.
 *
 * No-op if CONFIG_ZEGO_WIFI_P2P_CLIENT_TARGET_GO_MAC is empty or if the
 * CLIENT was not connected.
 */
void wifi_p2p_client_on_disconnect(void);

/**
 * @brief Notify P2P_CLIENT that a P2P peer was found during the pre-discovery scan.
 *
 * Called from the NET_EVENT_WIFI_P2P_DEVICE_FOUND handler.  If @p mac matches
 * CONFIG_ZEGO_WIFI_P2P_CLIENT_TARGET_GO_MAC, the function immediately cancels
 * the find phase and schedules the WIFI_P2P_CONNECT command.  wpa_supplicant
 * will use the peer's known operating channel for a targeted single-channel
 * BSS scan instead of the default full multi-channel scan, cutting connection
 * time from ~30 s to ~3-8 s.
 *
 * @param mac 6-byte P2P device address from the discovery event.
 */
void wifi_p2p_client_on_peer_found(const uint8_t *mac);

/**
 * @brief Assign a static IP (192.168.7.2/24) to the P2P_CLIENT Wi-Fi interface.
 *
 * Assigns the fixed client address for the P2P group (192.168.7.0/24) hosted
 * by the P2P_GO (192.168.7.1).  Removes any pre-existing address at that
 * slot first to avoid duplicate errors on reconnect.
 *
 * @param iface The Wi-Fi interface pointer from the CONNECT_RESULT callback.
 * @return 0 on success, negative errno on failure.
 */
int wifi_p2p_client_setup_static_ip(struct net_if *iface);

#endif /* WIFI_UTILS_H */
