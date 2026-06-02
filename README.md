# Zego — Reusable NCS/Zephyr modules

Shared module library for Nordic nRF Connect SDK applications.
Capabilities are composed by enabling Kconfig symbols — no code copying.

---

## Modules

| Module | Directory | zbus channels | Spec |
|--------|-----------|---------------|------|
| button | `zego/button/` | `BUTTON_CHAN` (out) | [button-spec.md](button/docs/button-spec.md) |
| led | `zego/led/` | `LED_CMD_CHAN` (in) · `LED_STATE_CHAN` (out) | [led-spec.md](led/docs/led-spec.md) |

See each spec for the full API, Kconfig reference, and hardware test guide.

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
CONFIG_ZEGO_BUTTON_BACKEND_DK=y
CONFIG_ZEGO_LED_NUM_LEDS=2
CONFIG_ZEGO_LED_BACKEND_DK=y

# nRF54LM20DK
CONFIG_ZEGO_BUTTON_NUM_BUTTONS=4
CONFIG_ZEGO_BUTTON_BACKEND_DK=y
CONFIG_ZEGO_LED_NUM_LEDS=4
CONFIG_ZEGO_LED_BACKEND_DK=y
```

To use the portable Zephyr backends instead (requires `gpio-leds` / `gpio-keys` DTS nodes):

```conf
CONFIG_ZEGO_BUTTON_BACKEND_GPIO=y
CONFIG_ZEGO_LED_BACKEND_ZEPHYR=y
```

> **Button and LED indices are 0-based.** Display names ("Button 1", "LED1") are
> board-specific and belong in the application.

---

## Repository layout

```
zego/
├── button/
│   ├── src/
│   │   ├── button.c / button.h      ← FSM + zbus core
│   │   ├── button_hw.h              ← HAL interface
│   │   ├── button_hw_dk.c           ← backend: dk_buttons_and_leds
│   │   └── button_hw_gpio.c         ← backend: Zephyr Input / gpio-keys
│   ├── Kconfig / CMakeLists.txt / boards/ / docs/ / sample/ / zephyr/module.yml
├── led/
│   ├── src/
│   │   ├── led.c / led.h            ← effects engine + zbus core
│   │   ├── led_hw.h                 ← HAL interface
│   │   ├── led_hw_dk.c              ← backend: dk_buttons_and_leds
│   │   └── led_hw_zephyr.c          ← backend: Zephyr gpio-leds / pwm-leds
│   ├── Kconfig / CMakeLists.txt / boards/ / docs/ / sample/ / zephyr/module.yml
└── west.yml   ← workspace manifest (NCS v3.3.0)
```

---

## Versioning

`zego` versions match the NCS version they target: `v<ncs-version>` (e.g. `v3.3.0`).
No build counter suffix is used — one release tag per NCS version.
The tag is pinned in each consuming application's `west.yml`.
Applications are released independently from their own repositories.

## License

[LicenseRef-Nordic-5-Clause](https://www.nordicsemi.com/About-us/Legal-information/Software-licence-agreement)
