# Validation Report — Nordic Wi-Fi App Template

## Document Information

| Field | Value |
|-------|-------|
| Version | 2026-06-29-23-18 |
| PRD Version | 2026-06-29-23-06 |
| Specs Version | 2026-06-29-23-06 |
| Plan Version | 2026-06-29-22-58 |
| Firmware build | P2P pairing feature (banner version tag 2026-06-29-21-44; functionally current) |
| Boards | 2× nRF5340 Audio DK + nRF7002EK (GO SN 1050136274, GC SN 1050111981) |
| ZView | No |
| Status | **FAIL** — pairing blocked by GO-side WPS Registrar init failure |

---

## Changelog

| Version | Summary |
|---------|---------|
| 2026-06-29-23-18 | Initial run — P2P button-pairing on 2× Audio DK. App logic validated; end-to-end pairing FAILS at GO WPS Registrar init. |
| 2026-06-30-11-10 | Retried with **WPS PBC** (switched GO `WIFI_WPS_PBC` + GC `pbc --join`; added GO-capability peer filter + double-click re-entrancy guard). GO fails **identically** (`Failed to initialize WPS Registrar` → AP never `AP-ENABLED`). Confirms the blocker is **not** PIN-vs-PBC and **not** the app layer — it is the GO hostapd WPS-Registrar/AP bring-up. Ruled out: WPS/AP/P2P/`MBEDTLS_DHM_C` all enabled; no heap/alloc error logged at INF. Root cause not yet isolated (needs supplicant DEBUG log or A/B vs nRF sample). |

---

## Executive Summary

| Metric | Result |
|--------|--------|
| TCs executed | 5 of 8 (R1; R2/R3 blocked by R1 failure) |
| Pass | TC-1, TC-2 (+ GC-side of TC-3) |
| Fail | TC-3 (end-to-end), TC-4, TC-5 |
| Blocked | TC-6, TC-7, TC-8 (depend on a successful first pair) |
| Verdict | **FAIL** — the new app logic (idle-until-paired, double-click trigger, discovery, connect-initiation) works as designed, but the underlying **P2P_GO WPS Registrar fails to initialize**, so no client can complete WPS and no GO MAC is ever learned/saved. |

---

## Per-TC Results

| TC | PRD | Criterion | Board | Result | Evidence |
|----|-----|-----------|-------|--------|----------|
| TC-1 | FR-006(1) | GO group + WPS PIN armed at boot | GO | ✅ PASS | `Active Wi-Fi mode: P2P_GO`; `P2P_GO: group created`; `WPS PIN active: 12345678` |
| TC-2 | FR-107(1) | Fresh GC idle, no auto-connect | GC | ✅ PASS | `P2P_GC: no saved GO - double-click Button 0 to pair` (no connect attempt) |
| TC-3 | FR-006(2)/FR-107(2) | Double-click pairs GO+GC | both | ❌ FAIL | GC side OK: `pairing - peer discovery (10 s)` → `peer table has 3 entries` → `pairing with GO B2:F2:F6:25:D4:4C RSSI=-53` → `connect initiated -> pin 12345678 --join`. **GO side fails**: `Failed to initialize WPS Registrar` / `Interface initialization failed` / `hapd_free_hapd_data: Interface wlan0 wasn't started` ~4.8 s after `group created`, every attempt. No `AP-ENABLED`, no `WPS-SUCCESS`, no `AP-STA-CONNECTED`. |
| TC-4 | FR-107(3) | GC saves GO MAC to NVS | GC | ❌ FAIL | No `saved GO … to NVS`; GC boots `no saved GO` after each attempt (WPS never completed) |
| TC-5 | FR-006(3) | GO logs peer connected, IP assigned | both | ❌ FAIL | No `AP-STA-CONNECTED` on GO; no `192.168.7.x` on GC |
| TC-6 | FR-107(5) | Reconnect after power cycle | GC | ⏸ BLOCKED | Requires a successful pair first |
| TC-7 | FR-107(4) | Reconnect after link drop | GC | ⏸ BLOCKED | Requires a successful pair first |
| TC-8 | FR-107(6) | Re-pair overwrites saved GO | GC | ⏸ BLOCKED | Requires a successful pair first |

---

## Failed-Test Detail

### P0 — GO WPS Registrar fails to initialize (root blocker)

**Expected** (per nRF P2P sample GO log): after `group_add`, `wlan0: interface state UNINITIALIZED->ENABLED` → `wlan0: AP-ENABLED` → `P2P-GROUP-STARTED` → WPS PIN armed → on client join `WPS-REG-SUCCESS` → `AP-STA-CONNECTED`.

**Actual** (GO board, all 3 attempts in the window, identical):
```
zego_wifi_utils: P2P_GO: creating group...
zego_wifi_utils: P2P_GO: group created - setting WPS PIN and starting discovery
wpa_supp: Failed to initialize WPS Registrar
wpa_supp: Interface initialization failed
wpa_supp: hapd_free_hapd_data: Interface wlan0 wasn't started
zego_wifi_utils: P2P_GO: WPS PIN active: 12345678        <-- our code logs success, but hostapd AP iface never came up
```
The app-layer `net_mgmt(WIFI_WPS_PIN_SET)` returns 0 (so our code logs "WPS PIN active"), but the wpa_supplicant/hostapd AP interface for the P2P group never reached `AP-ENABLED` — the WPS Registrar init failed, so the GO is not actually accepting WPS joins.

**Analysis**: This is in pre-existing GO code (`wifi_run_p2p_go_mode()` / `p2p_go_set_wps_pin_handler()` in `bricks/network/src/wifi_utils.c`) — **not** changed by the pairing feature. The nRF P2P sample proves the GO WPS path *can* work on this hardware/NCS, so the gap is config or sequencing in this project's GO bringup. Leading hypotheses:
1. **Sequence**: this project does `WIFI_P2P_GROUP_ADD` then a *separate*, deferred `WIFI_WPS_PIN_SET`; the nRF sample arms WPS as part of group formation. The separate WPS-set on a group whose AP iface hasn't finished coming up may be what fails the Registrar init.
2. **Missing Kconfig**: a WPS-AP / hostapd / crypto symbol the P2P sample enables but this project doesn't (WPS Registrar "uses TLS" + bignum per the NCS crypto-feature map).

**Route**: Phase 3.2 debug → compare this project's P2P_GO bringup + Wi-Fi/WPS Kconfig against `nrf/samples/wifi/p2p` (GO mode); likely a Phase 3.1 config/sequence fix. Re-run this plan after the fix.

### P2 — GC peer-selection has no GO-capability filter (secondary)
The GC's pairing discovery picked the highest-RSSI entry among **all** discovered P2P devices (peer table had 3: `FA:B9:5A…` -66, `B2:F2:F6…` -53, `1E:86:9A…` -75 — all locally-administered addresses; the -53 one is most likely our GO's P2P device address, the others likely nearby phones). With no filter for "is actually a P2P GO" (or for the intended peer), a closer non-GO P2P device in a busy RF environment could be selected. Acceptable for the one-GO bench case, but a robustness gap. Route: Phase 3.1 (next iteration) — filter the peer table to GO-capable devices, or confirm the selected peer is a GO before connecting.

---

## What the feature got right (app logic confirmed on hardware)
- P2P_GC boots **idle** with no saved GO and does not auto-connect (the removal of `TARGET_GO_MAC` works). → TC-2
- Double-click in P2P mode triggers `wifi_p2p_start_pairing()` on both roles (`app_ux: Double-click: triggering P2P pairing`). → mode-aware gesture works
- GC discovery → peer-table query → best-RSSI selection → `connect initiated -> pin 12345678 --join` all execute correctly.
- GO double-click logs `pairing window open` + re-arms WPS PIN.
- No crashes, no faults, builds/flashes clean on the Audio DK.

The blocker is strictly the GO-side WPS/hostapd bring-up, below the feature's app layer.

---

## Routing

| Priority | Finding | Route |
|----------|---------|-------|
| P0 | GO WPS Registrar init failure → pairing cannot complete | **Phase 3.2 debug** (compare GO bringup vs nRF P2P sample) then Phase 3.1 fix; re-run this plan |
| P2 | GC peer-selection lacks GO-capability filter | Phase 3.1 (next iteration) |
| Doc | Default mode + mode-cycle text corrected during this run (PRD/specs → 2026-06-29-23-06) | Done |

**Not ready for release.** Re-validate R1–R3 after the GO WPS issue is fixed.
