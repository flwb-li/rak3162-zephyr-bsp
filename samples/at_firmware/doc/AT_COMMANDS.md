<!--
Copyright (c) 2026 RAKwireless Technology Limited
SPDX-License-Identifier: Apache-2.0
-->

# RAK3162 AT Firmware Commands

This document describes the AT commands supported by the AT firmware in `samples/at_firmware`.
Target board: **RAK3162** (`rak3162/nrf54l15/cpuapp`) with onboard SX1262.

### Architecture (RUI3-like split)

| Layer | Location | Responsibility |
|-------|----------|----------------|
| Firmware framework | `modules/rak-fw` (in BSP) | Standard AT (system + LoRaWAN/P2P) + LoRa services |
| Board runtime | `lib/rak3162_runtime` | NVS, LED, bus PM, Sense, board ops bind |
| Application | `samples/at_firmware/src/main.c` | Startup order and auto-join policy |

Custom / `ATC+` style commands from RUI3 sketches are **not** required.
Product policy adds `AT+SENDINT` in the application layer.

Intended for **field / product use**: OTAA Class A, automatic join when credentials are stored,
configurable automatic uplinks via `AT+SENDINT`, and **System ON idle** between RF windows
(`AT+SLEEP` remains System OFF).
Command style follows the [RUI3 AT Command Manual](https://docs.rakwireless.com/product-categories/software-apis-and-libraries/rui3/at-command-manual/) as a **practical subset**.
Not implemented: ABP, Class B/C, P2P_FSK, CW / factory RF tests, LPSEND, LINKCHECK,
MASK/TXP, `AT+RETY`, and similar.

---

## 1. Serial interface

| Item | Description |
|------|-------------|
| AT UART | **UART20** (`zephyr,console`), 115200 8N1 |
| TX / RX | P1.06 / P1.07 |
| Line ending | Responses end with `\r\n` |
| Echo | Enabled by default (`ATE1` / `ATE0` supported by framework) |
| Boot print | On reset: `RAK3162 AT firmware ready` + AT command list (same as `AT?`) + `OK` |

---

## 2. Syntax and status codes

| Form | Meaning |
|------|---------|
| `AT+<CMD>?` | Help |
| `AT+<CMD>` | Execute |
| `AT+<CMD>=?` | Query |
| `AT+<CMD>=<args>` | Set |

| Status | Meaning |
|--------|---------|
| `OK` | Success (for async commands: **started**) |
| `AT_ERROR` | Generic error |
| `AT_PARAM_ERROR` | Parameter error |
| `AT_BUSY_ERROR` | Busy (join / send / P2P RX in progress, etc.) |
| `AT_NO_NETWORK_JOINED` | Not joined |
| `AT_NO_CLASSB_ENABLE` | Class B/C not supported |

Async events (may appear on UART at any time):

| Event | Description |
|-------|-------------|
| `+EVT:UART_WAKE` | Sense 唤醒完成且行缓冲已清空；收到后再发正式 AT |
| `+EVT:JOINED` | OTAA succeeded (app may start periodic uplink) |
| `+EVT:JOIN FAILED` | OTAA failed (finite attempts exhausted) |
| `+EVT:TX_DONE` | Uplink TX finished successfully |
| `+EVT:SEND_CONFIRMED_OK` / `FAILED` | Confirmed uplink result (`AT+CFM=1`) |
| `+EVT:RX_1:<rssi>:<snr>:UNICAST:<port>:<hex>` | Class A downlink |
| `+EVT:TXP2P DONE` | P2P TX finished |
| `+EVT:RXP2P:<rssi>:<snr>:<hex>` | P2P data received |
| `+EVT:RXP2P RECEIVE TIMEOUT` | P2P RX timeout |

---

## 3. Product boot behaviour

Sample `at_firmware` implements product policy in the application (`src/main.c`).
`rak-fw` only exposes join/send APIs and join/send-done callbacks.

1. Load NVS settings (OTAA keys, band, join options, `AT+SENDINT`).
2. If DevEUI / AppEUI / AppKey are missing, seed temporary test values
   `001122…` from `main.c` (override anytime with AT; remove before production).
3. Start the LoRaWAN stack. Any previously saved **data session is discarded**
   (RUI3-style). Credentials remain in NVS; LoRaMAC NVM keeps only monotonic
   **DevNonce** so rejoin does not reuse Nonce.
4. If `AT+NWM=1`, `join_auto=1`, and credentials are valid → start OTAA automatically.
5. On join success, if `AT+SENDINT` > 0, start automatic **unconfirmed** uplinks
   (auto cycle ignores `AT+CFM`; only manual `AT+SEND` uses confirm mode):
   - **Port:** `2`
   - **Payload:** 4-byte big-endian LoRaWAN FCnt
   - **Schedule:** every **`AT+SENDINT` seconds** (default **10**; `0` disables auto uplink)
     (`-EAGAIN` → retry without advancing the counter; no fake `TX_DONE`)
6. Between Join / TX / RX1 / RX2 windows the MCU uses **tickless idle** (WFI).
   See also `LOW_POWER.md`.
   **Before join:** AT UART (`uart20`) RX stays on for easy configuration.
   **After join:** between RF windows, `uart20` is suspended and **P1.07** is
   armed with GPIO Sense (`CONFIG_RAK_AT_UART_LP_KEEP_RX=n`). Host should use
   two-stage wake:
   1. send a dummy wake byte (e.g. `\r` / `0x00`)
   2. wait for `+EVT:UART_WAKE` (firmware settles ~50 ms and purges framing noise)
   3. send the real AT command
   (The wake byte itself is lost.) Optional `KEEP_RX=y` avoids loss but raises
   idle current substantially (~155 µA measured).
   Secondary buses (`uart21` / `i2c30` / `spi00`) are **resumed** before each RF window and
   **suspended** afterward via `pm_device`. LoRa `spi22` is suspended in the
   System ON idle prep path after SX1262 sleep.
   Radio operations run on a separate work queue; conflicting commands return `AT_BUSY_ERROR`.
   Note: Zephyr `CONFIG_PM` system states are **not** selected on nRF54L15 (`HAS_PM` absent).
7. `AT+SLEEP` still enters **System OFF** (deep sleep); optional RTC wakeup via `AT+RTC`.
   Before System OFF the firmware **stops** automatic `SENDINT` / join-retry works.
   System OFF cannot use AT UART wake the same way. Wake is a full reboot and
   requires a new OTAA join (automatically when `join_auto=1`).

### Idle current tip

After join, shallow-idle current should approach the UART-off regime (about 5 µA class with
SENSE wake), while AT remains wakeable via RX Sense — same idea as RUI3 LPM.

### Duty cycle note (EU868 default)

`AT+BAND=4` (EU868) enforces regional duty cycle inside LoRaMAC.
The firmware **schedules** sends per `AT+SENDINT`, but will **not** force RF when the stack
returns `-EAGAIN`. In that case TX is deferred and retried; no fake `+EVT:TX_DONE` is emitted.
Use a longer `AT+SENDINT` or another `AT+BAND` if duty cycle limits apply.

---

## 4. Network mode and region

### 4.1 `AT+NWM` — working mode

| Value | Meaning |
|-------|---------|
| `0` | P2P_LORA |
| `1` | LoRaWAN (default) |
| `2` | P2P_FSK (**not supported** → `AT_PARAM_ERROR`) |

Switching stops P2P RX; the board does **not** auto-reset like RUI3. Persisted in Settings.

P2P write operations (`P2P` / `PRECV` / `PSEND` SET) require `AT+NWM=0`.
LoRaWAN `JOIN` / `SEND` require `AT+NWM=1`.

### 4.2 `AT+BAND` — region

RUI3 region numbers (default **4 = EU868**). Persisted in Settings.
May be changed while idle (not joining / not sending). If currently joined,
the session is dropped (`AT+NJS=0`) and a new OTAA is required.

---

## 5. OTAA credentials and join

Typical provisioning (override or confirm the seeded test credentials):

```text
AT+DEVEUI=0011223344556677
AT+APPEUI=0011223344556677
AT+APPKEY=00112233445566778899AABBCCDDEEFF
AT+BAND=4
AT+SENDINT=10
AT+JOIN=1:1:8:0
ATZ
```

After reboot, with keys stored and `join_auto=1`, the device performs a **new OTAA**
(no restored data session). Automatic counter uplinks run only when `AT+SENDINT`
is non-zero and are always unconfirmed.

| Command | Description |
|---------|-------------|
| `AT+DEVEUI` / `AT+APPEUI` / `AT+APPKEY` / `AT+NWKKEY` | OTAA credentials (written to NVS immediately; changing them does **not** leave the current session by itself) |
| `AT+DEVADDR` | Device address (8 hex chars). When OTAA-joined, `=?` returns the NS-assigned DevAddr. SET stores NVS for ABP prep (ABP join not implemented). |
| `AT+JOIN` | `AT+JOIN=<cmd>:<auto>:<interval_s>:<attempts>`. `cmd=0` stops an in-progress join and stores `auto`. `cmd=1` starts a **new OTAA** even if currently joined. Does **not** stop `AT+SENDINT` by itself. |
| `AT+NJS` | Local join status (`1` while the stack thinks it is joined) |
| `AT+SENDINT` | Auto uplink interval in seconds (`0` = off, default `10`) |
| `AT+SEND` | Manual uplink `<port>:<hex>` (uses `AT+CFM`) |
| `AT+CFM` / `AT+CFS` | Confirm mode for **manual** `AT+SEND` / last confirm status |
| `AT+ADR` | Adaptive data rate (default ON; applied at stack start from NVS) |
| `AT+CLASS` | Class A only |
| `AT+RECV` | Last downlink `<port>:<hex>` |

Rejoin (RUI3-like):

```text
AT+JOIN=1:0:8:3     # force new OTAA now
# or
ATZ                 # reboot; auto OTAA if join_auto=1
```

---

## 6. LoRa P2P

```text
AT+NWM=0
AT+P2P=868000000:7:125:0:8:14
AT+PRECV=65535
AT+PSEND=48656C6C6F
```

Factory CW / BLE CW / `AT+TEST` commands are **removed** from product firmware.

---

## 7. System commands

| Command | Description |
|---------|-------------|
| `AT` / `AT?` | Attention / list help |
| `ATZ` | MCU reset (no `OK`; silent reboot with current power-test build) |
| `ATE0` / `ATE1` | Echo off / on |
| `AT+VER` | Firmware version |
| `AT+SN` | Serial number |
| `AT+SLEEP` / `AT+RTC` | System OFF deep sleep / RTC wakeup delay (seconds) |

Low-power / session modes:

| Mode | How | LoRaWAN data session |
|------|-----|----------------------|
| System ON idle | Automatic between LoRaWAN windows (GRTC + Sense) | Kept in RAM for this run |
| `ATZ` / power cycle | MCU reset | Lost; OTAA again (`join_auto=1`) |
| System OFF | `AT+SLEEP=<delay_ms>` (+ optional `AT+RTC`) | Lost; OTAA again after reboot |

Use `AT+<CMD>?` for per-command help.

---

## 8. Build

```bash
west build -b rak3162/nrf54l15/cpuapp samples/at_firmware --no-sysbuild
```
