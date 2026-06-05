# Nordic Wi-Fi App Template

A minimal, production-ready starting point for NCS Wi-Fi applications targeting nRF7x development kits.

Out of the box it connects your device to Wi-Fi in any of four modes and fires a callback when connectivity is established. Add your application logic to `src/modules/network/net_event_app.c` and go.

---

## What's included

- **All four Wi-Fi modes**: STA, SoftAP, P2P_GO, P2P_CLIENT
- **All three STA provisioning methods**: shell one-time, saved credentials (`wifi cred`), BLE provisioning via the *nRF Wi-Fi Provisioner* phone app
- **Runtime mode switching**: `app_wifi_mode [sta|softap|p2p_go|p2p_client]` — persisted in flash
- **Button + LED channels**: `BUTTON_CHAN` and `LED_CMD_CHAN` wired up and ready to use
- **Startup banner**: mode, connection instructions, and compiled module list printed at boot
- **A single customisation point**: `net_event_app.c` with inline guide for publishing your own zbus channel

---

## Supported boards

| Board | Build target | BLE provisioning |
|---|---|---|
| nRF7002DK | `nrf7002dk/nrf5340/cpuapp` | Disabled (flash full) |
| nRF54LM20DK + nRF7002EB2 | `nrf54lm20dk/nrf54lm20a/cpuapp` + `-DSHIELD=nrf7002eb2` | Enabled |
| nRF5340 Audio DK + nRF7002EK | `nrf5340_audio_dk/nrf5340/cpuapp` + `-DSHIELD=nrf7002ek` | Disabled (flash full) |

---

## Quick start

### 1. Build

```bash
# nRF7002DK — STA + SoftAP (no P2P)
cd zego/nordic-wifi-app-template
west build -p -b nrf7002dk/nrf5340/cpuapp -d build_nrf7002dk

# nRF7002DK — all four modes including P2P_GO / P2P_CLIENT
west build -p -b nrf7002dk/nrf5340/cpuapp -d build_nrf7002dk -- -Dnordic-wifi-app-template_SNIPPET=wifi-p2p

# nRF54LM20DK + nRF7002EB2 — STA + SoftAP (no P2P)
west build -p -b nrf54lm20dk/nrf54lm20a/cpuapp -d build_nrf54lm20dk -- -DSHIELD=nrf7002eb2

# nRF54LM20DK + nRF7002EB2 — all four modes including P2P
west build -p -b nrf54lm20dk/nrf54lm20a/cpuapp -d build_nrf54lm20dk -- \
  -Dnordic-wifi-app-template_SNIPPET=wifi-p2p -DSHIELD=nrf7002eb2

# nRF5340 Audio DK + nRF7002EK — STA + SoftAP (no P2P)
west build -p -b nrf5340_audio_dk/nrf5340/cpuapp -d build_nrf5340_audio_dk -- -DSHIELD=nrf7002ek

# nRF5340 Audio DK + nRF7002EK — all four modes including P2P
west build -p -b nrf5340_audio_dk/nrf5340/cpuapp -d build_nrf5340_audio_dk -- \
  -Dnordic-wifi-app-template_SNIPPET=wifi-p2p -DSHIELD=nrf7002ek
```

> The image-scoped `-Dnordic-wifi-app-template_SNIPPET=wifi-p2p` applies the snippet only to the app
> image (not to `hci_ipc`), avoiding spurious Kconfig warnings on the net core.
> The snippet adds `CONFIG_WIFI_NM_WPA_SUPPLICANT_P2P=y` and `CONFIG_NRF70_P2P_MODE=y`.
> Without it, `p2p_go` and `p2p_client` modes are unavailable at runtime.

> **Toolchain wrapper** (if not in a west shell):
> ```bash
> nrfutil sdk-manager toolchain launch --ncs-version=v3.3.0 -- west build ...
> ```

### 2. Flash

```bash
# First flash (erases NVS — re-enter Wi-Fi credentials after)
west flash -d build_nrf7002dk --erase          # nRF7002DK
west flash -d build_nrf54lm20dk --recover      # nRF54LM20DK
west flash -d build_nrf5340_audio_dk --erase   # nRF5340 Audio DK

# Subsequent flashes (preserves NVS / saved credentials)
west flash -d build_nrf7002dk
west flash -d build_nrf54lm20dk
west flash -d build_nrf5340_audio_dk
```

### 3. Connect via shell (STA mode)

Open a serial terminal at **115200 baud** (nRF7002DK: VCOM1; nRF54LM20DK / nRF5340 Audio DK: VCOM0).

```
uart:~$ wifi connect -s MyNetwork -p MyPassword -k 1
```

The default mode on a fresh flash is **P2P_GO**. Switch to STA first if needed:

```
uart:~$ app_wifi_mode sta
# device reboots into STA mode
uart:~$ wifi connect -s MyNetwork -p MyPassword -k 1
```

### 4. Save credentials (auto-connect on every boot)

```
uart:~$ wifi cred add MyNetwork WPA2-PSK MyPassword -k 1
# reboot — device connects automatically
```

### 5. BLE provisioning (nRF54LM20DK only)

1. Flash the firmware (BLE prov is enabled automatically on nRF54LM20DK)
2. Open the **nRF Wi-Fi Provisioner** app on your phone
3. Select the device (name shown in the startup banner, e.g. `PV4A2F1B`)
4. Enter your Wi-Fi credentials — the device connects and saves them to flash

---

## Wi-Fi mode switching

```
uart:~$ app_wifi_mode sta          # join an existing network
uart:~$ app_wifi_mode softap       # create a hotspot at 192.168.7.1
uart:~$ app_wifi_mode p2p_go       # Wi-Fi Direct: device is the group owner
uart:~$ app_wifi_mode p2p_client   # Wi-Fi Direct: device joins phone's group
```

Mode is saved to NVS and takes effect after reboot. Default on fresh flash: `p2p_go`.

---

## Add your application logic

Edit `src/modules/network/net_event_app.c`. The file contains inline instructions and a 4-step example for publishing a zbus channel when Wi-Fi connects.

```c
void zego_network_on_wifi_connected(enum zego_wifi_mode mode, const char *ip_addr,
                                    const char *mac_addr, const char *ssid)
{
    LOG_INF("Wi-Fi connected: mode=%d ip=%s", mode, ip_addr);
    /* TODO: publish your app zbus channel here */
}

void zego_network_on_wifi_disconnected(void)
{
    LOG_INF("Wi-Fi disconnected");
    /* TODO: publish your app zbus channel here */
}
```

For button events, subscribe to `BUTTON_CHAN`. For LED control, publish to `LED_CMD_CHAN`.

---

## Documentation

| Doc | Path |
|---|---|
| PRD | [docs/pm-prd/PRD.md](docs/pm-prd/PRD.md) |
| Engineering specs overview | [docs/dev-specs/overview.md](docs/dev-specs/overview.md) |
| Architecture | [docs/dev-specs/architecture.md](docs/dev-specs/architecture.md) |
| Wi-Fi mode selector (zego/wifi) | [zego/modules/wifi/docs/wifi-spec.md](https://github.com/chshzh/zego/blob/main/modules/wifi/docs/wifi-spec.md) |
| Network module (zego/network) | [zego/modules/network/docs/network-spec.md](https://github.com/chshzh/zego/blob/main/modules/network/docs/network-spec.md) |
| BLE provisioning (zego/wifi_ble_prov) | [zego/modules/wifi_ble_prov/docs/wifi-ble-prov-spec.md](https://github.com/chshzh/zego/blob/main/modules/wifi_ble_prov/docs/wifi-ble-prov-spec.md) |
| Button module (zego/button) | [zego/modules/button/docs/button-spec.md](https://github.com/chshzh/zego/blob/main/modules/button/docs/button-spec.md) |
| LED module (zego/led) | [zego/modules/led/docs/led-spec.md](https://github.com/chshzh/zego/blob/main/modules/led/docs/led-spec.md) |
