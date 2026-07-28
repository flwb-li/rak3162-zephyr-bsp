<!--
Copyright (c) 2026 RAKwireless Technology Limited
SPDX-License-Identifier: Apache-2.0
-->

# RAK3162 HW_TEST AT Commands

This document describes the AT commands supported by the `samples/hw_test` firmware.
Target board: **RAK3162** (`rak3162/nrf54l15/cpuapp`) with onboard SX1262.

Intended for **practical use** (OTAA / uplink / downlink / P2P), not a full RUI3 module firmware.
Command style follows the [RUI3 AT Command Manual](https://docs.rakwireless.com/product-categories/software-apis-and-libraries/rui3/at-command-manual/) as a **practical subset**.
Not implemented: ABP, Class B/C, P2P_FSK, LPSEND, LINKCHECK, MASK/TXP, and similar.

---

## 1. Serial interface

| Item | Description |
|------|-------------|
| AT UART | **UART20** (`zephyr,console`), 115200 8N1 |
| TX / RX | P1.06 / P1.07 |
| Line ending | Responses end with `\r\n` |
| Echo | Enabled by default (`ATE1`) |

Secondary UART (`AT+TEST=UART`): **UART21**, TX=P2.08, RX=P2.07.

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
| `+EVT:JOINED` | OTAA succeeded |
| `+EVT:JOIN FAILED` | OTAA failed |
| `+EVT:TX_DONE` | Uplink TX finished |
| `+EVT:SEND_CONFIRMED_OK` / `FAILED` | Confirmed uplink result (`AT+CFM=1`) |
| `+EVT:RX_1:<rssi>:<snr>:UNICAST:<port>:<hex>` | Class A downlink |
| `+EVT:TXP2P DONE` | P2P TX finished |
| `+EVT:RXP2P:<rssi>:<snr>:<hex>` | P2P data received |
| `+EVT:RXP2P RECEIVE TIMEOUT` | P2P RX timeout |

---

## 3. Network mode and region

### 3.1 `AT+NWM` — working mode

| Value | Meaning |
|-------|---------|
| `0` | P2P_LORA |
| `1` | LoRaWAN (default) |
| `2` | P2P_FSK (**not supported** → `AT_PARAM_ERROR`) |

Switching stops P2P RX; the board does **not** auto-reset like RUI3. Persisted in Settings.

P2P write operations (`P2P` / `PRECV` / `PSEND` / `CW` SET) require `AT+NWM=0`.
LoRaWAN `JOIN` / `SEND` require `AT+NWM=1`.

### 3.2 `AT+BAND` — region

RUI3 region numbers:

| Value | Region | This firmware |
|-------|--------|---------------|
| 0 | EU433 | Supported |
| 1 | CN470 | Supported |
| 2 | RU864 | Supported |
| 3 | IN865 | Supported |
| 4 | EU868 | Supported (default) |
| 5 | US915 | Supported |
| 6 | AU915 | Supported |
| 7 | KR920 | Supported |
| 8 | AS923-1 | Supported (maps to Zephyr AS923) |
| 9–12 | AS923-2/3/4, LA915 | **Not supported** → `AT_PARAM_ERROR` |

Must be set **before the LoRaWAN stack starts** (before the first `AT+JOIN`). Changing BAND after start → `AT_BUSY_ERROR`.

The RF hardware band must match the selected region (LF/HF module variants).

---

## 4. LoRaWAN keys / join / send

### 4.1 Credentials

| Command | Description |
|---------|-------------|
| `AT+DEVEUI` | 16 hex digits (8 bytes) |
| `AT+APPEUI` | 16 hex digits (8 bytes, JoinEUI) |
| `AT+APPKEY` | 32 hex digits (16 bytes) |
| `AT+NWKKEY` | 32 hex digits (16 bytes; optional — if unset, join uses APPKEY) |

### 4.2 `AT+NJM` / `AT+NJS` / `AT+CFM` / `AT+CFS` / `AT+ADR` / `AT+CLASS` / `AT+RECV`

| Command | Description |
|---------|-------------|
| `AT+NJM` | OTAA only (`=1`); ABP → `AT_PARAM_ERROR` |
| `AT+NJS=?` | `0` not joined / `1` joined |
| `AT+CFM` | `0`/`1`: whether `AT+SEND` uses confirmed uplink (default 0) |
| `AT+CFS=?` | Whether the last confirmed send succeeded |
| `AT+ADR` | `0`/`1` ADR |
| `AT+CLASS` | Class A only; B/C → `AT_NO_CLASSB_ENABLE` |
| `AT+RECV=?` | Last downlink `<port>:<hex>`; cleared to `0:` after read |

### 4.3 `AT+JOIN`

Aligned with [RUI3 AT+JOIN](https://docs.rakwireless.com/product-categories/software-apis-and-libraries/rui3/at-command-manual/):

```text
AT+JOIN?
AT+JOIN=?
AT+JOIN
AT+JOIN=1:0:10:8
AT+JOIN=0
```

| Param | Meaning | Default |
|-------|---------|---------|
| Param1 | `1`=start join, `0`=stop join | — |
| Param2 | Auto-join on power-up (`0`/`1`) | `0` |
| Param3 | Retry interval in seconds (`0` or 7–255; `0` means use default 8) | `8` |
| Param4 | Attempt count; `0`=retry until success or `AT+JOIN=0` | `0` |

Behavior:
- Async: returns `OK` immediately; finishes with `+EVT:JOINED` or (attempts exhausted) `+EVT:JOIN FAILED`
- Intermediate failures do not emit EVT; retries use Param3 (same as RUI3)
- `AT+JOIN=?` returns `AT_BUSY_ERROR` while joining
- Use `AT+NJS=?` for join state, not `JOIN=?`

### 4.4 `AT+SEND`

```text
AT+SEND=<port>:<payload>
```

Example: `AT+SEND=2:010203`
Returns `OK` immediately, then `+EVT:TX_DONE`; if `CFM=1`, followed by `SEND_CONFIRMED_OK` / `FAILED`.

---

## 5. LoRa P2P

Default parameters (RAM): `868000000:7:125:0:8:14`. Requires `AT+NWM=0`.

| Command | Description |
|---------|-------------|
| `AT+P2P=<freq>:<sf>:<bw>:<cr>:<preamble>:<power>` | bw: `0`=125 kHz, `1`=250 kHz, `2`=500 kHz |
| `AT+PRECV=<time>` | `0` stop; `1`..`65532` timed RX (ms); `65533` continuous (TX allowed); `65534` continuous locked; `65535` until one packet (same as RUI3) |
| `AT+PSEND=<hex>` | After TX: `+EVT:TXP2P DONE` |
| `AT+CW=<freq>:<power>:<time_ms>` | Continuous wave |

---

## 6. Typical flows

### LoRaWAN uplink / downlink

```text
AT+NWM=1
AT+BAND=4
AT+DEVEUI=<16 hex>
AT+APPEUI=<16 hex>
AT+APPKEY=<32 hex>
AT+CFM=1
AT+JOIN
OK
+EVT:JOINED
AT+NJS=?
AT+NJS=1
OK
AT+SEND=2:48656C6C6F
OK
+EVT:TX_DONE
+EVT:SEND_CONFIRMED_OK
+EVT:RX_1:-80:5:UNICAST:2:AA55FF
AT+RECV=?
AT+RECV=2:AA55FF
OK
```

### P2P

```text
AT+NWM=0
AT+P2P=868000000:7:125:0:8:14
AT+PRECV=65535
AT+PSEND=48656C6C6F
```

---

## 7. Other commands (non-LoRa)

| Command | Description |
|---------|-------------|
| `AT` / `AT?` | Attention / list help |
| `ATZ` | MCU reset |
| `ATE0` / `ATE1` | Echo off / on |
| `AT+VER` | Firmware version |
| `AT+BUILDTIME` | Build timestamp |
| `AT+SN` | Serial number |
| `AT+HFXOCAP` / `AT+LFXOCAP` | HFXO / LFXO load capacitance |
| `AT+SLEEP` / `AT+RTC` | System OFF sleep / RTC wakeup delay |
| `AT+TEST` | Hardware self-tests (e.g. UART loopback) |
| `AT+BLECW` / `AT+BLECWSTOP` | BLE continuous-wave test |

Use `AT+<CMD>?` for per-command help.

---

## 8. Build

```bash
west build -b rak3162/nrf54l15/cpuapp samples/hw_test --no-sysbuild
```
