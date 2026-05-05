# Zego — Zephyr + Lego

> **Build applications like Lego: snap reusable modules together.**

Zego is a shared modules library for Nordic nRF Connect SDK (NCS) Wi-Fi IoT applications.
Every functional capability — button handling, network management, LED feedback, web serving —
lives as a self-contained module that any application can opt-in to via Kconfig.
Applications are composed by enabling modules, not by copying code.

Each application lives in its own standalone repository and pulls `zego` in as a west project.

## Repository layout

```
zego/                           # this repo — common modules + workspace manifest
├── modules/                    # Shared, reusable functional modules (CONFIG_ZEGO_*)
│   ├── button/
│   ├── network/
│   ├── led/
│   ├── CMakeLists.txt
│   └── Kconfig
├── utils/                      # Lightweight helpers with no app-level state
├── scripts/                    # Build and CI helper scripts
├── docs/
├── west.yml                    # Workspace manifest — pinned to NCS v3.3.0
└── .github/workflows/ci.yml
```

When the workspace is initialized, the layout becomes:

```
<workspace>/
├── nrf/                        # NCS (sdk-nrf v3.3.0)
├── zephyr/                     # via NCS import
├── bootloader/, modules/, ...  # via NCS import
├── zego/                       # this repo
├── nordic-wifi-webdash/        # application repo
└── nordic-wifi-memfault/       # application repo
```

## NCS baseline

The NCS version is pinned in `west.yml` (for example **v3.3.0**).
`zego/west.yml` is the single workspace manifest; each application repo also carries its own
`west.yml` for standalone development.

## Workspace setup

### Method 1 — Add to an existing NCS installation

If you already have a matching NCS version installed,
reuse it directly — no re-downloading required.

Under terminal with toolchain:
```sh
cd /opt/nordic/ncs/<ncs-version>   # your existing NCS workspace root

git clone https://github.com/chshzh/zego.git

# Switch the workspace manifest from nrf → zego (one-time change)
west config manifest.path zego

# Sync — NCS repos already present, only new project repos are cloned
west update
```

### Method 2 — Fresh installation as a Workspace Application

#### Option A: nRF Connect for VS Code

Follow the [custom repository guide](https://docs.nordicsemi.com/bundle/nrf-connect-vscode/page/guides/extension_custom_repo.html)

#### Option B: CLI

See the Nordic guide on [Workspace Application Setup](https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/dev_model_and_contributions/adding_code.html#workflow_4_workspace_application_repository_recommended).

```sh
west init -m https://github.com/chshzh/zego --mr main <workspace-dir>
cd <workspace-dir>
west update
```

Build an application (run from the workspace root):

```sh
# webdash — nRF7002 DK
west build -b nrf7002dk/nrf5340/cpuapp nordic-wifi-webdash -- -DSNIPPET=wifi-p2p

# webdash — nRF54L-M20 DK with nRF7002EB2 shield
west build -b nrf54lm20dk/nrf54lm20a/cpuapp nordic-wifi-webdash -- -DSNIPPET=wifi-p2p -DSHIELD=nrf7002eb2

# memfault — nRF7002 DK
west build -b nrf7002dk/nrf5340/cpuapp nordic-wifi-memfault -- -DOVERLAY_CONFIG=overlay-app-memfault-project-info.conf
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

Each application lives in its own repository and is released independently using its own
`VERSION` file and `vX.Y.Z` tags. `zego` itself is versioned separately and pinned in each
application's `west.yml`.

The repo uses a single `main` branch. Feature and fix branches are short-lived.

## License

[LicenseRef-Nordic-5-Clause](https://www.nordicsemi.com/About-us/Legal-information/Software-licence-agreement)
