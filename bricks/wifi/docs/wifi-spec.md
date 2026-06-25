# App Main Module Specification

## Document Information

| Field | Value |
|-------|-------|
| Module | `zego/app_main` |
| Version | 2026-06-05-09-31 |
| PRD Version | N/A (standalone library module) |
| Status | Stable |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-06-04-00-00 | Initial module spec — banner merged in from zego/banner; Wi-Fi mode selector; shell command; NVS persistence |
| 2026-06-05-09-31 | Added nRF5340 Audio DK + nRF7002EK to Supported Hardware table |

---

## Overview

The `zego/app_main` module is the **complete application entry point** for NCS Wi-Fi
projects.  It provides three interlocking responsibilities in a single module:

1. **Startup banner** — prints app name, version, PRD, specs, build date, board, and
   MAC at boot via `print_banner()`.  Two override points allow projects to inject
   Wi-Fi instructions and module lists without touching the shared code.

2. **Wi-Fi mode selector** — persists the active Wi-Fi mode (STA / SoftAP / P2P_GO /
   P2P_CLIENT) to NVS via the Zephyr `settings` subsystem.  Publishes the loaded mode
   on `WIFI_MODE_CHAN` at `SYS_INIT APPLICATION` priority 0 so every network module
   starts in the correct mode.

3. **`zego_wifi_mode` shell command** — allows runtime mode switching.  Saves the new
   mode to NVS, then reboots.  Guarded by `#if CONFIG_SHELL` so it compiles out when
   the shell is disabled.

The module replaces the former `zego/banner` library (deleted June 2026).  The banner
logic is now embedded directly in `app_main.c`; no separate library or Kconfig symbol
is needed.

---

## Location

- **Path**: `zego/app_main/`
- **Files**: `src/app_main.c`, `src/app_main.h`, `src/wifi_mode_selector.c`,
  `Kconfig`, `CMakeLists.txt`, `zephyr/module.yml`, `sample/`, `docs/`

---

## Module Type

- [x] **Application module** — driven by `SYS_INIT` for mode loading; `main()` for
  banner and sleep loop.  No dedicated thread.  Shell command registered via
  `SHELL_CMD_ARG_REGISTER` when `CONFIG_SHELL=y`.

---

## Supported Hardware

| Board | Build target | Notes |
|-------|-------------|-------|
| nRF54LM20DK + nRF7002EB2 | `nrf54lm20dk/nrf54lm20a/cpuapp` + `-DSHIELD=nrf7002eb2` | Same capabilities; larger flash/RAM |
| nRF7002DK | `nrf7002dk/nrf5340/cpuapp` | STA + SoftAP (`NRF70_AP_MODE`); P2P with `-DSNIPPET=wifi-p2p` |
| nRF5340 Audio DK + nRF7002EK | `nrf5340_audio_dk/nrf5340/cpuapp` + `-DSHIELD=nrf7002ek` | STA + SoftAP + P2P; ~1 MB app-core flash (same as nRF7002DK); network core runs `hci_ipc` for BLE (`SB_CONFIG_NETCORE_HCI_IPC=y`); application must provide a DTS board overlay mapping the nRF7002EK SPI bus to the Audio DK GPIO pins |

---

## Zbus Integration

**Publishes to**: `WIFI_MODE_CHAN` — once at `SYS_INIT APPLICATION` priority 0.

```c
enum zego_wifi_mode {
    ZEGO_WIFI_MODE_STA        = 0,  /* join an existing Wi-Fi network   */
    ZEGO_WIFI_MODE_SOFTAP     = 1,  /* create an access point           */
    ZEGO_WIFI_MODE_P2P_GO     = 2,  /* P2P Group Owner (Wi-Fi Direct)   */
    ZEGO_WIFI_MODE_P2P_CLIENT = 3,  /* join a peer's P2P group          */
};

struct wifi_mode_msg {
    enum zego_wifi_mode mode;
};
```

> **NVS compat note**: enum values are stored as `uint8_t`.  Do not reorder or
> renumber them without running `west flash --erase` (or `--recover`) on all
> devices to reset NVS.

**Subscribes to**: nothing — mode is loaded from NVS at init, not driven by zbus.

---

## Override Points (weak functions)

Projects override these two functions in their own source files to inject
project-specific content into the banner.  Both have no-op defaults in
`app_main.c`.

| Function | Responsibility |
|----------|---------------|
| `void zego_banner_wifi_info(void)` | Print Wi-Fi mode string + per-mode connection instructions.  Must end with a `"============"` separator line. |
| `void zego_banner_app_extra(void)` | Print a compiled-modules list or any other project info.  Must end with a `"============"` separator line. |

`app_main.c` itself provides the override implementations conditioned on all
relevant `CONFIG_*` symbols — SoftAP, P2P, BLE prov, Wi-Fi shell, etc.

---

## NVS Persistence

Settings key: `"app/zego_wifi_mode"` (uint8_t).

| Event | Behaviour |
|-------|-----------|
| Key absent (fresh flash) | Default from `ZEGO_APP_MAIN_DEFAULT_WIFI_MODE_*` Kconfig choice |
| Key present | Loaded in `settings_set_cb`; stored in `static enum zego_wifi_mode s_mode` |
| Mode changed via shell | `settings_save_one("app/zego_wifi_mode", ...)` then `sys_reboot(SYS_REBOOT_COLD)` |

The storage partition must be large enough for the settings subsystem.  For
nRF7002DK with Memfault the `storage_partition` is 8 KB — sufficient for this
single key plus Wi-Fi credentials.

---

## Shell Command

Available when `CONFIG_SHELL=y`.

```
zego_wifi_mode [softap|sta|p2p_go|p2p_client]
```

- With no argument: prints current mode and usage hint.
- With argument: validates availability (`NRF70_AP_MODE` / `NRF70_P2P_MODE`),
  saves to NVS, reboots.  Unsupported modes return `-EINVAL` with an explanation.

---

## Kconfig Flags

| Symbol | Type | Default | Description |
|--------|------|---------|-------------|
| `CONFIG_ZEGO_APP_MAIN` | bool | n | Enable this module; depends on `NETWORKING && ZBUS` |
| `CONFIG_ZEGO_APP_MAIN_DEFAULT_WIFI_MODE_STA` | choice | **default** | Default mode on fresh flash |
| `CONFIG_ZEGO_APP_MAIN_DEFAULT_WIFI_MODE_SOFTAP` | choice | n | Requires `NRF70_AP_MODE` |
| `CONFIG_ZEGO_APP_MAIN_DEFAULT_WIFI_MODE_P2P_GO` | choice | n | Requires `NRF70_P2P_MODE` |
| `CONFIG_ZEGO_APP_MAIN_DEFAULT_WIFI_MODE_P2P_CLIENT` | choice | n | Requires `NRF70_P2P_MODE` |
| `CONFIG_ZEGO_APP_MAIN_LOG_LEVEL` | int | 4 (INF) | Log level (0=off … 4=debug) |

**Selects automatically** (no manual prj.conf entries needed):
`SETTINGS`, `FLASH`, `FLASH_MAP`, `FLASH_PAGE_LAYOUT`, `REBOOT`.

---

## CMakeLists — App Name Injection

The app name shown in the banner header is derived from the application source
directory name at build time:

```cmake
get_filename_component(_BANNER_APP_NAME ${APPLICATION_SOURCE_DIR} NAME)
zephyr_library_compile_definitions(ZEGO_BANNER_APP_NAME="${_BANNER_APP_NAME}")
```

Projects do not need to set anything — the name is automatic.

---

## Version String Injection

Projects must supply three compile-time string macros.  The recommended pattern
in the project's `CMakeLists.txt`:

```cmake
# Git-describe version (falls back to "dev")
zephyr_compile_definitions(APP_VERSION_STRING="...")

# PRD and specs versions extracted from their markdown tables
zephyr_compile_definitions(PRD_VERSION="...")
zephyr_compile_definitions(SPECS_VERSION="...")
```

The sample provides stub values directly via `prj.conf` for standalone testing.

---

## Boot Sequence

```
SYS_INIT APPLICATION prio 0  — wifi_mode_selector_init()
  • settings_load_subtree("app") → s_mode set
  • zbus_chan_pub(WIFI_MODE_CHAN, {.mode = s_mode}, K_NO_WAIT)

main()
  • print_banner(APP_VERSION_STRING, PRD_VERSION, SPECS_VERSION)
      – header block (name, version, PRD, specs, build, board, MAC)
      – zego_banner_wifi_info()   ← project override
      – zego_banner_app_extra()   ← project override
  • while(1) { k_sleep(K_FOREVER); }
```

---

## Memory Estimate

| Component | Flash | RAM |
|-----------|-------|-----|
| `app_main.c` (banner + main) | ~3 KB | 0 B static |
| `wifi_mode_selector.c` (settings + shell) | ~2 KB | 4 B (`s_mode`) |
| Zephyr settings subsystem (if not already pulled in) | ~6 KB | ~1 KB |
| **Total (settings already enabled)** | **~5 KB** | **~4 B** |

---

## Test Points

Expected UART output on a clean boot with default STA mode:

```
[zego_app_main] ==============================================
[zego_app_main] my-app-name
[zego_app_main] ==============================================
[zego_app_main] Version: v1.0.0
[zego_app_main] PRD:     2026-01-01-00-00
[zego_app_main] Specs:   2026-01-01-00-00
[zego_app_main] Build:   Jun  4 2026 ...
[zego_app_main] Board:   nRF7002DK
[zego_app_main] MAC:     XX:XX:XX:XX:XX:XX
[zego_app_main] ==============================================
[zego_app_main] Mode:    STA
[zego_app_main] ==============================================
[zego_app_main] Connection instructions:
[zego_app_main] STA mode:
[zego_app_main] ...
[zego_app_main] ==============================================
[zego_app_main] Compiled modules:
[zego_app_main] ...
[zego_app_main] ==============================================
```

Shell command test:
```
uart:~$ zego_wifi_mode softap
Switching to SoftAP mode -- rebooting...
```
After reboot, `Mode: SoftAP` should appear in the banner.
