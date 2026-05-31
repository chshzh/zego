# Zego — Reusable NCS/Zephyr modules

Shared module library for Nordic nRF Connect SDK applications.
Capabilities are composed by enabling Kconfig symbols — no code copying.

---

## Modules

| Module  | Directory              | zbus channels                                       | Spec |
|---------|------------------------|-----------------------------------------------------|------|
| button  | `zego/button/`         | `BUTTON_CHAN` (out)                                  | [button-spec.md](button/docs/button-spec.md) |
| led     | `zego/led/`            | `LED_CMD_CHAN` (in) · `LED_STATE_CHAN` (out)          | [led-spec.md](led/docs/led-spec.md) |
| network | `zego/modules/network/`| `NETWORK_CHAN` (out)                                 | — |

See each spec for the full API, Kconfig reference, and examples.

---

## Workspace setup

### Add to an existing NCS installation

```sh
cd /opt/nordic/ncs/<ncs-version>
git clone https://github.com/chshzh/zego.git
west config manifest.path zego
west update
```

### Fresh workspace

```sh
west init -m https://github.com/chshzh/zego --mr main <workspace-dir>
cd <workspace-dir>
west update
```

---

## Integration

Register modules as first-class Zephyr modules via `EXTRA_ZEPHYR_MODULES`
in the app's `CMakeLists.txt` **before** `find_package(Zephyr ...)`.
Kconfig is wired automatically — no `rsource` needed.

```cmake
cmake_minimum_required(VERSION 3.20.0)

get_filename_component(ZEGO_BUTTON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../zego/button REALPATH)
get_filename_component(ZEGO_LED_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/../zego/led    REALPATH)
list(APPEND EXTRA_ZEPHYR_MODULES ${ZEGO_BUTTON_DIR} ${ZEGO_LED_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
```

`prj.conf`:

```conf
CONFIG_ZEGO_BUTTON=y
CONFIG_ZEGO_LED=y
```

`boards/<board>.conf`:

```conf
# nRF7002DK
CONFIG_ZEGO_BUTTON_NUM_BUTTONS=2
CONFIG_ZEGO_LED_NUM_LEDS=2

# nRF54LM20DK
CONFIG_ZEGO_BUTTON_NUM_BUTTONS=3
CONFIG_ZEGO_LED_NUM_LEDS=4
```

> **Button indices are 0-based.** Display names ("Button 1", "BUTTON0") are
> board-specific and belong in the application via its own `app_button_label()` helper.

---

## Consumer applications

| App                  | Board        | Build command |
|----------------------|--------------|---------------|
| nordic-wifi-webdash  | nRF7002DK    | `west build -b nrf7002dk/nrf5340/cpuapp nordic-wifi-webdash -- -DSNIPPET=wifi-p2p` |
| nordic-wifi-webdash  | nRF54LM20DK  | `west build -b nrf54lm20dk/nrf54lm20a/cpuapp nordic-wifi-webdash -- -DSNIPPET=wifi-p2p -DSHIELD=nrf7002eb2` |
| nordic-wifi-memfault | nRF7002DK    | `west build -b nrf7002dk/nrf5340/cpuapp nordic-wifi-memfault -- -DEXTRA_CONF_FILE=overlay-app-memfault-project-info.conf` |
| nordic-wifi-memfault | nRF54LM20DK  | `west build -b nrf54lm20dk/nrf54lm20a/cpuapp nordic-wifi-memfault -- -DSHIELD=nrf7002eb2 -DEXTRA_CONF_FILE=overlay-app-memfault-project-info.conf` |

---

## Repository layout

```
zego/
├── button/    ← button module (src/, Kconfig, CMakeLists.txt, boards/, docs/, test/, zephyr/module.yml)
├── led/       ← LED module
├── modules/   ← aggregation shim + network module
└── west.yml   ← workspace manifest (NCS v3.3.0)
```

---

## Versioning

`zego` is versioned with `vX.Y.Z` tags and pinned in each application's `west.yml`.
Applications are released independently from their own repositories.

## License

[LicenseRef-Nordic-5-Clause](https://www.nordicsemi.com/About-us/Legal-information/Software-licence-agreement)
