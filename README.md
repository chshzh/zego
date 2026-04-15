# Zego — Zephyr + Lego

> **Build applications like Lego: snap reusable modules together.**

Zego is a Nordic nRF Connect SDK (NCS) monorepo for Wi-Fi connected IoT applications.
Every functional capability — button handling, network management, LED feedback, web serving —
lives as a self-contained module that any application can opt-in to via Kconfig.
Applications are composed by enabling modules, not by copying code.

## Repository layout

```
zego/
├── applications/               # Zephyr app roots — each has its own CMakeLists.txt + prj.conf
│   ├── nordic-wifi-memfault/
│   ├── nordic-wifi-webdash/
│   └── nordic-wifi-audio/
├── modules/                    # Shared, reusable functional modules (CONFIG_ZEGO_*)
│   ├── button/
│   ├── network/
│   ├── led/
│   ├── CMakeLists.txt
│   └── Kconfig
├── utils/                      # Lightweight helpers with no app-level state
├── scripts/                    # Build and CI helper scripts
├── docs/
│   └── migration.md
├── west.yml                    # Single manifest — pinned to NCS v3.2.4
└── .github/workflows/ci.yml
```

## NCS baseline

All applications in this repo build against **NCS v3.2.4**.
The manifest is the single source of truth; never activate an app-local `west.yml`.

## Building applications

Initialize the workspace once:

```sh
west init -l zego
west update
```

Build an application:

```sh
# webdash — nRF7002 DK
west build -b nrf7002dk/nrf5340/cpuapp applications/nordic-wifi-webdash -- -DSNIPPET=wifi-p2p

# webdash — nRF54L-M20 DK with nRF7002EB2 shield
west build -b nrf54lm20dk/nrf54lm20a/cpuapp applications/nordic-wifi-webdash -- -DSNIPPET=wifi-p2p -DSHIELD=nrf7002eb2

# memfault — nRF7002 DK
west build -b nrf7002dk/nrf5340/cpuapp applications/nordic-wifi-memfault -- -DOVERLAY_CONFIG=overlay-app-memfault-project-info.conf

# audio — nRF7002 DK (gateway role)
west build -b nrf7002dk/nrf5340/cpuapp applications/nordic-wifi-audio
```

Flash:

```sh
west flash
```

## Module system

Shared modules live under `modules/`. Each module:

- Has its own `CMakeLists.txt` and `Kconfig.<module>` file.
- Is guarded by a `CONFIG_ZEGO_<MODULE>` Kconfig symbol.
- Exposes a clean header interface and uses zbus for inter-module messaging.

Applications opt-in explicitly:

```cmake
# In the application CMakeLists.txt
add_subdirectory(${ZEGO_MODULES_DIR}/button button)
```

```kconfig
# In prj.conf
CONFIG_ZEGO_BUTTON=y
```

## Versioning and releases

Each application carries its own `VERSION` file and is released with an app-scoped tag:

```
nordic-wifi-memfault/v1.0.0
nordic-wifi-webdash/v1.0.0
nordic-wifi-audio/v1.0.0
```

The repo uses a single `main` branch. Feature and fix branches are short-lived.

## License

[LicenseRef-Nordic-5-Clause](https://www.nordicsemi.com/About-us/Legal-information/Software-licence-agreement)
