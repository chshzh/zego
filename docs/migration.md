# Zego Migration Guide

## Overview

This document tracks the staged migration of three standalone Nordic Wi-Fi apps into
the `zego` monorepo. Each phase has clear success criteria before the next begins.

---

## Phase 1 — Bootstrap ✅

**Goal:** Single Git repo, single west manifest, empty shared scaffolding.

Completed tasks:
- Created `/opt/nordic/ncs/myApps/zego` as the only Git repository.
- Added `west.yml` pinned to NCS `v3.2.4` with `self.path: zego`.
- Created `modules/`, `utils/`, `scripts/`, `applications/` skeleton.
- Added `modules/CMakeLists.txt` and `modules/Kconfig` with `CONFIG_ZEGO_*` placeholders.
- Created `.github/workflows/ci.yml` with matrix build scaffold.

**Decisions recorded:**
- Root is an orchestration repo — no `find_package(Zephyr)` at the root level.
- Each `applications/<app>` keeps its own `CMakeLists.txt` / `prj.conf` / `sysbuild.conf`.
- Shared module symbols are prefixed `CONFIG_ZEGO_` to avoid namespace collisions.

---

## Phase 2 — Import applications (pending)

**Goal:** All three apps live under `applications/` with Git history preserved.

Order of imports:
1. `nordic-wifi-memfault` → `applications/nordic-wifi-memfault`
2. `nordic-wifi-webdash` → `applications/nordic-wifi-webdash`
3. `nordic-wifi-audio` → `applications/nordic-wifi-audio`

Import command pattern (using `git subtree`):
```sh
git -C /opt/nordic/ncs/myApps/zego subtree add \
    --prefix=applications/nordic-wifi-memfault \
    /opt/nordic/ncs/myApps/nordic-wifi-memfault main --squash
```
> Use `--squash` for a clean merge commit, or omit it to rewrite full history.

After each import:
- Archive the app-local `west.yml` (rename to `west.yml.archived`).
- Update app `README.md` to reference `west build applications/<app> ...` commands.
- Add a `VERSION` file at the app root.

---

## Phase 3 — Validate on NCS v3.2.4 (pending)

**Goal:** Each app builds cleanly from the zego workspace root.

Build matrix to verify:

| App | Board | Extra flags |
|-----|-------|-------------|
| nordic-wifi-memfault | `nrf7002dk/nrf5340/cpuapp` | `-DOVERLAY_CONFIG=overlay-app-memfault-project-info.conf` |
| nordic-wifi-memfault | `nrf54lm20dk/nrf54lm20a/cpuapp` | `-DSHIELD=nrf7002eb2` |
| nordic-wifi-webdash | `nrf7002dk/nrf5340/cpuapp` | `-DSNIPPET=wifi-p2p` |
| nordic-wifi-webdash | `nrf54lm20dk/nrf54lm20a/cpuapp` | `-DSNIPPET=wifi-p2p -DSHIELD=nrf7002eb2` |
| nordic-wifi-audio | `nrf7002dk/nrf5340/cpuapp` | — |
| nordic-wifi-audio | `nrf54lm20dk/nrf54lm20a/cpuapp` | `-DSHIELD=nrf7002eb2` |

`nordic-wifi-audio` targets NCS `v3.2.1` in its current manifest; normalize it to
`v3.2.4` as part of this phase. Do NOT split gateway/headset yet.

---

## Phase 4 — Extract shared modules (pending)

**Goal:** Shared code lives in `modules/`; apps opt-in via `CONFIG_ZEGO_*`.

Extraction order (lowest risk first):
1. `button/` — identical in memfault and webdash; safe first candidate.
2. `network/` (common infrastructure) — shared Wi-Fi STA lifecycle code.
3. `led/` — trivial, good for validating the extraction pattern.

Extraction steps for each module:
1. Copy sources from one of the apps into `modules/<name>/`.
2. Rename internal Kconfig symbol `CONFIG_<OLD>` → `CONFIG_ZEGO_<NAME>`.
3. Update `modules/CMakeLists.txt` and `modules/Kconfig` to include the module.
4. Update each consuming app's `CMakeLists.txt` to reference `ZEGO_MODULES_DIR`.
5. Update each consuming app's `Kconfig` to `osource` the zego Kconfig root.
6. Delete the now-redundant app-local copy.
7. Build all apps and confirm no regressions.

Defer audio-specific extraction (codec, audio I²S, HW HAL) until audio is stable on `v3.2.4`.

---

## Phase 5 — CI and release automation (pending)

**Goal:** Matrix CI from the monorepo root with per-app release tags.

CI behavior:
- Changes under `applications/<app>/**` → build that app's board matrix only.
- Changes under `modules/**`, `utils/**`, or root CI files → build all apps.
- Tag `<app>/v*` → build the tagged app and publish artifacts.

Release convention:
- Each app has `applications/<app>/VERSION` as the source of truth.
- Release tags are app-scoped: `nordic-wifi-memfault/v1.0.0`.
- No repo-wide version tag.

---

## Appendix: Module opt-in wiring pattern

### In `applications/<app>/CMakeLists.txt`

```cmake
# Point to the shared modules directory relative to the app root
set(ZEGO_MODULES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../modules)

# Include shared module aggregation (each module guards itself with CONFIG_ZEGO_*)
include(${ZEGO_MODULES_DIR}/CMakeLists.txt)
```

### In `applications/<app>/Kconfig`

```kconfig
# Pull in all shared Zego module Kconfig symbols
osource "$(ZEGO_MODULES_DIR)/Kconfig"
```

### In `applications/<app>/prj.conf`

```kconfig
CONFIG_ZEGO_BUTTON=y
CONFIG_ZEGO_LED=y
CONFIG_ZEGO_NETWORK=y
```
