# Validation Plan — Nordic Wi-Fi App Template

## Document Information

| Field | Value |
|---|---|
| Project | nordic-wifi-app-template |
| Version | 2026-06-29-22-58 |
| PRD Version | 2026-06-29-21-44 |
| Specs Version | 2026-06-29-21-44 |
| NCS Version | v3.3.0 |
| Run type | Post-feature: P2P button-pairing UX — core pairing + power-cycle reconnect (FR-006, FR-107) |
| Boards | 2× nRF5340 Audio DK + nRF7002EK (SN 1050136274 = GO, SN 1050111981 = GC) |
| ZView | No (deferred) |
| Status | Draft — pending execution |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-06-16-14-32 | Initial plan — 3 rounds covering all P0 FRs + selected P1s; ZView in Round 2 |
| 2026-06-29-22-58 | Re-scoped for P2P button-pairing feature: 3 rounds on a 2× Audio DK pair (no shell — button-driven + log observe). Covers FR-006 pairing window, FR-107 pair/persist/reconnect incl. power-cycle. |

---

## Board Addressing Map

| Role | Board | Serial (`--dev-id`) | App-log VCOM | Build dir | Gesture button |
|------|-------|---------------------|--------------|-----------|----------------|
| **P2P_GO** | nRF54LM20DK + nRF7002EB2 | 1051839157 | `/dev/tty.usbmodem0010518391571` (rtscts=True) | `build_nrf54lm20dk` | BUTTON0 (idx 0) |
| **P2P_GC** | nRF5340 Audio DK + nRF7002EK | 1050111981 | `/dev/tty.usbmodem0010501119811` | `build_nrf5340_audio_dk` | **BTN5 (idx 4)** |

> Run note (2026-06-30): the original GO Audio DK (1050136274) dropped off USB mid-flash, so the GO role moved to the nRF54LM20DK. The GO gesture button is therefore idx 0 (BUTTON0); the BTN5/idx-4 gesture is validated on the GC (Audio DK) only.

> The Audio DK build has `CONFIG_SHELL=y`. Wi-Fi **mode** is set over the UART shell with `zego_wifi_mode p2p_go` / `zego_wifi_mode p2p_gc` (saves to NVS + reboots). The **pairing trigger** (double-click Button 0 = VOL-) and **power-cycle** are still physical actions by the operator. Default mode on fresh flash is STA.

---

## Coverage Matrix

| TC | PRD ref | Criterion | Round | Execution |
|----|---------|-----------|-------|-----------|
| TC-1 | FR-006 (1) | P2P_GO creates group + arms WPS PIN at boot | R1 | Log observe (GO) |
| TC-2 | FR-107 (1) | Fresh P2P_GC with no saved GO stays idle (does not auto-connect) | R1 | Log observe (GC) |
| TC-3 | FR-006 (2) / FR-107 (2) | Double-click GO opens pairing window; double-click GC discovers + joins via `pin 12345678 --join` | R1 | [HUMAN] button + log |
| TC-4 | FR-107 (3) | On successful pair, GC saves GO MAC to NVS (`net/p2p_gc_go_mac`) | R1 | Log: "saved GO … to NVS" |
| TC-5 | FR-006 (3) | GO logs peer connected; static IP 192.168.7.x assigned | R1 | Log observe (both) |
| TC-6 | FR-107 (5) | **Power-cycle GC → reconnects to saved GO automatically, no button, ~30 s** | R2 | [HUMAN] power-cycle + log |
| TC-7 | FR-107 (4) | GC reconnects automatically after a link drop (GO power-cycle) | R2 | [HUMAN] power-cycle GO + log |
| TC-8 | FR-107 (6) | New double-click pairing on GC overwrites the saved GO (re-pair = forget) | R3 | [HUMAN] button + log |

---

## Test Rounds

### Round R1 — Pair from scratch
1. Flash both Audio DKs (erase NVS → both boot fresh in default STA).
2. Set roles over the shell: `zego_wifi_mode p2p_go` on the GO board, `zego_wifi_mode p2p_gc` on the GC board (each saves to NVS + reboots).
3. Confirm **GC** log: `Current Wi-Fi Mode: P2P_GC` + `P2P_GC: no saved GO - double-click Button 0 to pair`. → **TC-2**
4. Confirm **GO** log: group created, `P2P_GO: WPS PIN active: 12345678`. → **TC-1**
4. **[HUMAN]** Double-click Button 0 on **GO** → log `pairing window open`. → **TC-3a**
5. **[HUMAN]** Double-click Button 0 on **GC** → `pairing - peer discovery`, then `pairing with GO …`, `connect initiated -> pin 12345678 --join`. → **TC-3b**
6. Confirm **GC** log: `saved GO … to NVS`, `connected to GO`. → **TC-4**
7. Confirm **GO** log: peer connected. → **TC-5**

### Round R2 — Reconnect after power cycle (key criterion)
1. **[HUMAN]** Power-cycle the **GC** board (reset/replug).
2. Confirm **GC** boot log: `saved GO … - reconnecting`, then `connected to GO` within ~30 s, **no button press**. → **TC-6**
3. **[HUMAN]** Power-cycle the **GO** board; confirm GC logs disconnect then reconnect when GO returns. → **TC-7**

### Round R3 — Re-pair overwrites
1. **[HUMAN]** Double-click Button 0 on **GO** (open window), then double-click Button 0 on **GC**.
2. Confirm **GC** log: pairing runs again and `saved GO … to NVS` (overwrites). → **TC-8**

---

## Execution Notes
- Operator presses buttons; the assistant monitors both UART logs (vcom0) via `chsh-ag-terminal` and records evidence lines.
- "Double-click" window = `CONFIG_ZEGO_BUTTON_DOUBLE_CLICK_WINDOW_MS` (400 ms) — two quick presses.
- Results captured in `docs/qa-test/VALIDATION_REPORT.md`.
