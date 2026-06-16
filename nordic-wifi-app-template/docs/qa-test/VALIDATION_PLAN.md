# Validation Plan — Nordic Wi-Fi App Template

## Document Information

| Field | Value |
|---|---|
| Project | nordic-wifi-app-template |
| Version | 2026-06-16-14-32 |
| PRD Version | 2026-06-16-13-30 |
| Specs Version | 2026-06-16-13-30 |
| NCS Version | v3.3.0 |
| Run type | Routine smoke test (post-feature: SoftAP max-3-clients + TODO log format) |
| Boards | nRF7002DK (SN 1050793110), nRF54LM20DK+nRF7002EB2 (SN 1051869687) |
| nRF5340 Audio DK | Not connected — same binary config as nRF7002DK; results apply by construction |
| ZView | Yes — both boards, during Round 2 (SoftAP sustained load) |
| Status | Approved |

---

## Changelog

| Version | Summary of changes |
|---|---|
| 2026-06-16-14-32 | Initial plan — 3 rounds covering all P0 FRs + selected P1s; ZView in Round 2 |

---

## Board Addressing Map

| Board | SN | App shell port | rtscts | Baud | J-Link target | App ELF |
|---|---|---|---|---|---|---|
| nRF7002DK | 1050793110 | `/dev/tty.usbmodem0010507931103` | False | 115200 | `nRF5340_xxAA` | `build_nrf7002dk/nordic-wifi-app-template/zephyr/zephyr.elf` |
| nRF54LM20DK+nRF7002EB2 | 1051869687 | `/dev/tty.usbmodem0010518696871` | True | 115200 | `nRF54LM20A_M33` | `build_nrf54lm20dk/nordic-wifi-app-template/zephyr/zephyr.elf` |

---

## Coverage Matrix

| TC | PRD ID | Priority | Criterion (abbreviated) | Method | Round | Board(s) |
|---|---|---|---|---|---|---|
| TC-001 | FR-001 | P0 | Clean build — binary already present, verify boot banner printed | Log parse | R1 | both |
| TC-002 | FR-002 | P0 | STA shell connect → DHCP IP logged | Shell | R1 | both |
| TC-003 | FR-003 | P0 | `wifi cred add` → reboot → auto-reconnect | Shell + reboot | R1 | both |
| TC-004 | FR-004 | P0 | BLE prov (nRF54LM20DK only) | **SKIP** — needs phone; out of scope for this run | — | — |
| TC-005a | FR-005 | P0 | SoftAP: up to 3 clients join 192.168.7.1 | Shell + [HUMAN] connect phone/laptop | R2 | both |
| TC-005b | FR-005 | P0 | TODO log format: "now X/3 devices connected" on every connect/disconnect | Log parse | R2 | both |
| TC-006 | FR-006 | P0 | P2P_GO auto-starts at boot; WPS PBC armed; DK-as-CLIENT connects within 30 s | Log parse | R3 | nRF7002DK=GO |
| TC-007 | FR-007 | P0 | Mode persists across reboot | Shell + reboot | R2 | both |
| TC-008 | FR-008 | P0 | `zego_on_net_event_dhcp_bound()` fires with mode/ip/mac/ssid | Log parse | R1 | both |
| TC-009 | FR-101 | P1 | Button press logs `button_msg` on `BUTTON_CHAN` | Log parse after button press | R1 | both |
| TC-010 | FR-103 | P1 | Heap high-water mark logged periodically | Log parse | R1 | both |
| TC-011 | FR-104 | P1 | Long-press Button 0 ≥ 3 s → mode cycles + reboot | **[HUMAN]** press button | R2 | both |
| TC-012 | FR-105 | P1 | LED ROTATE while connecting; solid ON when connected; ROTATE when SoftAP no clients | **[HUMAN]** visual | R1+R2 | both |
| TC-013 | FR-106 | P1 | Double-click toggles BLE prov (nRF54LM20DK) | **SKIP** — BLE prov out of scope this run | — | — |
| TC-014 | FR-107 | P1 | P2P_CLIENT auto-connects to GO using `TARGET_GO_MAC`; static IP 192.168.7.2/24 assigned | Log parse | R3 | nRF54LM20DK=CLIENT |
| TC-015 | FR-108 | P1 | MAC-prefix mode: `TARGET_GO_MAC` ending `:00:00:00` → scan + best RSSI | **SKIP** — requires custom build; out of scope this run | — | — |

---

## Test Rounds

### Round 1 — Boot + STA Connect \[both boards\] \[ZView start\]

**Objective**: Verify clean boot banner, STA shell connect, saved-cred reconnect, net_event hook,
button log, heap log.

**Covers**: TC-001, TC-002, TC-003, TC-008, TC-009, TC-010, TC-012 (connecting phase)

**ZView**: Start recording on both boards at the beginning of this round. Keep running through Round 2.

**Prerequisites**: Both boards flashed with current build (already done). Default mode = P2P_GO on fresh NVS — switch to STA first.

**Steps** (both boards independently, nRF54LM20DK shell: rtscts=True, nRF7002DK shell: rtscts=False):

1. Open UART on board. Capture boot log. Verify banner `Nordic Wi-Fi App Template` printed.
2. Switch to STA mode and reboot:
   ```
   app_wifi_mode sta
   ```
3. After reboot, connect to AP:
   ```
   wifi connect -s BE92U_5G -k 1 -p @BillionWIFI
   ```
4. Wait for DHCP IP. Verify log contains:
   - `TODO: Device has IP ... (mac=... ssid=...)` ← TC-008
   - `[net_event_app]` log printed ← TC-001 banner check pass
5. Check heap log printed within 2 minutes: `heap_monitor` or similar periodic line ← TC-010
6. Press Button 0 once (short press). Verify `button_msg` log appears ← TC-009
7. **[HUMAN]** Observe LED 0: ROTATE while connecting → solid ON when connected ← TC-012
8. Add saved credential and reboot to test auto-reconnect:
   ```
   wifi cred add BE92U_5G WPA2-PSK @BillionWIFI -k 1
   ```
   Then reset board:
   ```
   kernel reboot cold
   ```
9. After reboot, verify auto-reconnect (no manual `wifi connect`). Log must show DHCP IP ← TC-003

**Evidence to record**: Boot banner lines, DHCP log line, heap log line, button_msg log line, IP address.

---

### Round 2 — SoftAP + Mode Persistence \[both boards\] \[ZView]\]

**Objective**: Verify SoftAP brings up hotspot, clients connect with correct TODO log format,
mode persists across reboot. Long-press mode cycle.

**Covers**: TC-005a, TC-005b, TC-007, TC-011, TC-012 (SoftAP LED phase)

**ZView**: Active from Round 1 through this round. This round is the peak-watermark round for ZView.

**Steps** (both boards):

1. Switch to SoftAP mode:
   ```
   app_wifi_mode softap
   ```
   Board reboots. Verify log: `SoftAP: connect to ...` banner.
2. **[HUMAN]** Observe LED 0: should ROTATE (AP up, no clients) ← TC-012
3. **[HUMAN]** Connect a phone or laptop to the SoftAP SSID. Verify log:
   ```
   net_event_app: AP client connected: now 1/3 devices connected
   ```
   ← TC-005a, TC-005b
4. **[HUMAN]** Connect a second device. Verify log:
   ```
   net_event_app: AP client connected: now 2/3 devices connected
   ```
5. **[HUMAN]** Disconnect one device. Verify log:
   ```
   net_event_app: AP client disconnected: now 1/3 devices connected
   ```
6. **[HUMAN]** Disconnect last device. Verify log:
   ```
   net_event_app: AP client disconnected: now 0/3 devices connected
   ```
   Observe LED returns to ROTATE ← TC-012
7. Verify mode persists — reboot and check SoftAP re-arms without shell command:
   ```
   kernel reboot cold
   ```
   After reboot: log must show SoftAP banner (not STA) ← TC-007
8. **[HUMAN]** Long-press Button 0 for ≥ 3 s. Verify log shows mode changed and device reboots into next mode ← TC-011

**Evidence to record**: TODO log lines with X/3 format, mode-persists log after reboot, button long-press reboot log.

---

### Round 3 — P2P GO + CLIENT Pair \[cross-board\] \[ZView stop\]

**Objective**: Verify P2P_GO auto-starts and DK-as-P2P_CLIENT auto-connects using TARGET_GO_MAC.

**Covers**: TC-006, TC-014

**Board assignment**:
- **nRF7002DK** → P2P_GO
- **nRF54LM20DK** → P2P_CLIENT

**Steps**:

1. On nRF7002DK: switch to P2P_GO mode:
   ```
   app_wifi_mode p2p_go
   ```
   After reboot, capture boot log. Note the MAC address printed in banner:
   `P2P_GO mode: group up, PBC armed - this DK's MAC: XX:XX:XX:XX:XX:XX`
   Record as `<GO_MAC>`.
2. Verify nRF7002DK log: P2P group created at boot; WPS PBC armed ← TC-006 (partial)
3. On nRF54LM20DK: set `TARGET_GO_MAC` to `<GO_MAC>` and switch to P2P_CLIENT:
   ```
   app_wifi_mode p2p_client
   ```
   > **Note**: `CONFIG_ZEGO_WIFI_P2P_CLIENT_TARGET_GO_MAC` is a build-time Kconfig. If the
   > current build has a different MAC configured, use the shell `wifi p2p connect <GO_MAC> pbc --join`
   > fallback method to verify connectivity manually instead.
4. Verify nRF54LM20DK log within 30 s:
   - `auto-connecting to GO <GO_MAC>` ← TC-014 criterion 1
   - `zego_on_net_event_dhcp_bound()` called with `mode=p2p_client ip=192.168.7.2` ← TC-014 criterion 2
5. Verify nRF7002DK log: client association logged ← TC-006 complete
6. Stop ZView recording on both boards.

**Evidence to record**: P2P_GO boot MAC log, CLIENT "auto-connecting to GO" log, CLIENT dhcp_bound log with IP 192.168.7.2.

---

## ZView Configuration

**Prerequisite Kconfig** (check current build has these — board conf already sets them via `CONFIG_THREAD_MONITOR` etc.):
```
CONFIG_INIT_STACKS=y
CONFIG_THREAD_MONITOR=y
CONFIG_THREAD_STACK_INFO=y
CONFIG_SYS_HEAP_RUNTIME_STATS=y
CONFIG_THREAD_NAME=y
```

**Record commands** (run in NCS terminal, one per board, start before Round 1):
```bash
# nRF7002DK
west zview record \
  -e build_nrf7002dk/nordic-wifi-app-template/zephyr/zephyr.elf \
  -r jlink -t nRF5340_xxAA -s 1050793110 \
  -o /tmp/zview_nrf7002dk.ndjson.gz --duration 600

# nRF54LM20DK
west zview record \
  -e build_nrf54lm20dk/nordic-wifi-app-template/zephyr/zephyr.elf \
  -r jlink -t nRF54LM20A_M33 -s 1051869687 \
  -o /tmp/zview_nrf54lm20dk.ndjson.gz --duration 600
```

**Extract peaks** (after recording stops):
```bash
west zview dump -i /tmp/zview_nrf7002dk.ndjson.gz --json \
  | jq '.threads[] | {name, alloc:.stack_size, watermark:.runtime.stack_watermark_percent}'
west zview dump -i /tmp/zview_nrf54lm20dk.ndjson.gz --json \
  | jq '.threads[] | {name, alloc:.stack_size, watermark:.runtime.stack_watermark_percent}'
```

---

## Out of Scope (this run)

| TC | FR | Reason |
|---|---|---|
| TC-004 | FR-004 | BLE provisioning — requires phone; schedule separately |
| TC-013 | FR-106 | BLE double-click — same dependency |
| TC-015 | FR-108 | MAC-prefix mode — requires custom `CONFIG_ZEGO_WIFI_P2P_CLIENT_TARGET_GO_MAC` build; schedule separately |
