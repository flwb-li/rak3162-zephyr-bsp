<!--
Copyright (c) 2026 RAKwireless Technology Limited
SPDX-License-Identifier: Apache-2.0
-->

# Low-power behaviour (at_firmware)

RAK3162 product firmware sends an automatic LoRaWAN uplink on a configurable
interval (`AT+SENDINT`, default 10 s). Between uplinks the MCU remains in
**System ON idle** and the SX1262 uses **warm sleep** (`WarmStart=1`).

## Automatic uplink cycle

1. Boot initializes Settings/NVS and the LoRaWAN stack. OTAA runs when
   credentials are valid; an old data session is not restored across reset
   (RUI3-style). LoRaMAC NVM remains enabled only to preserve monotonic
   DevNonce state. Missing DevEUI/AppEUI/AppKey are currently seeded with
   temporary `001122…` test values from `main.c`.
2. If `AT+SENDINT` is non-zero, Port 2 sends the next LoRaWAN FCnt as a
   four-byte big-endian **unconfirmed** payload on that interval (`0` disables
   auto uplink; use `AT+SEND` manually, which respects `AT+CFM`).
3. After the Class A TX/RX cycle, SX1262 enters warm sleep with
   `SetSleep(WarmStart=1)`. ANT_SW and secondary buses are already disabled by
   the RF-window exit hooks; idle prep then suspends LoRa `spi22` and arms AT
   UART Sense.
4. A Zephyr delayable work timeout, backed by the GRTC system timer, schedules
   the next uplink. With no runnable work the CPU executes WFI in System ON.
5. Before the next RF window, SPI/ANT_SW resume; the next SPI access wakes
   SX1262 with its register configuration retained.

Packet-to-packet timing follows **`AT+SENDINT` seconds between uplink queue marks**.
Class A RX1/RX2 elapsed time is subtracted from the idle delay. There is no
MCU cold-boot budget because System ON retains execution state.

## Runtime idle (between RF windows)

| Mechanism | Behaviour |
|-----------|-----------|
| Tickless kernel | MCU waits in System ON WFI; timeout wake is driven by GRTC |
| LF clock | GRTC uses the board LFXO (32.768 kHz, internal 8 pF load setting) |
| MCU RAM | All CPUAPP RAM (188 KB) remains powered and retained |
| SX1262 | Warm sleep (`WarmStart=1`); config retained across idle |
| ANT_SW | Board regulator gates RTC66006 VDD for the whole Join/TX/RX call |
| Secondary buses | `uart21` / `i2c30` / `spi00` suspended between LoRaWAN jobs |
| LoRa SPI | `spi22` suspended in System ON idle prep and resumed before the next RF window |
| AT UART Sense | P1.07 GPIO SENSE wake (`sense-edge-mask`); first wake byte may be lost (`+EVT:UART_WAKE`). Set `KEEP_RX=y` to avoid loss (~155 µA). |
| Logging | Disabled in `prj.conf` for idle-current measurements (`CONFIG_LOG=n`) |

The nRF54L15 contains 256 KB total SRAM, but this Zephyr CPUAPP image owns
188 KB. The remaining 68 KB belongs to the FLPR domain and is not part of the
CPUAPP linker region. System ON naturally retains all 188 KB; no retained-memory
partition is required.

## AT console wake (two-stage)

Default (`CONFIG_RAK_AT_UART_LP_KEEP_RX=n`):

1. Firmware suspends UART and arms GPIO Sense on P1.07 when entering console LP.
2. Host sends a dummy wake byte (e.g. `\r` / `0x00`). That byte is lost.
3. Firmware restores UART, waits ~50 ms, purges framing noise from the AT line
   buffer, then emits `+EVT:UART_WAKE`.
4. Host sends the real AT command.

Optional `CONFIG_RAK_AT_UART_LP_KEEP_RX=y` keeps UARTE async RX so no byte is
lost, but idle current rises substantially (~155 µA measured).

Boot is silent (`CONFIG_LOG=n`, `CONFIG_BOOT_BANNER=n`): after `ATZ` / power-on
there is no banner; send `AT` to confirm the console is alive.

## Deep sleep (`AT+SLEEP`)

| Item | Notes |
|------|-------|
| Mode | System OFF (`sys_poweroff`) |
| Optional wake | `AT+RTC=<seconds>` arms GRTC before sleep |
| State | RAM and the active LoRaWAN data session are lost; OTAA runs again after reboot |
| Prep | SX1262 warm sleep + ANT_SW off; LEDs, secondary buses, and LoRa SPI suspended |

This explicit command is intentionally different from the automatic uplink
cycle. Its GRTC wake is a reset-style cold boot (`RESET_CLOCK`), not a
continuation after `sys_poweroff()`. RAM is not retained in this path.

## RF window hooks

`lorawan_join()` / `lorawan_send()` are wrapped with board RF enter/exit:

- enter → resume LoRa SPI, enable ANT_SW, resume secondary buses and AT UART;
  warm-start SX1262 if it was sleeping
- exit → ANT_SW off, suspend secondary buses; AT Sense is armed when the
  LoRaWAN RF-state handler / System ON idle prep enters UART LP

Peripherals stay active for the whole blocking Class A cycle (TX + RX1 + RX2).

## Duty cycle

EU868 (default `AT+BAND=4`) enforces regional duty cycle inside LoRaMAC.
If an automatic uplink returns `-EAGAIN`, firmware does not emit a fake
`+EVT:TX_DONE`; it warm-sleeps the SX1262 and retries after a GRTC-timed
System ON idle interval. ADR follows NVS/`AT+ADR` (default ON).

## Related files

- `lib/rak3162_runtime/src/board/board_at_lp.c` — Sense / AT UART LP
- `lib/rak3162_runtime/src/core/bus_pm.c` — secondary bus PM
- `lib/rak3162_runtime/src/radio/radio_bind.c` — SPI/ANT_SW/System ON idle prep
- `samples/at_firmware/src/main.c` — join + uplink policy / `AT+SENDINT`
- `modules/rak-fw/src/lora/lorawan.c` — SX1262 warm sleep/wake; discard restored
  data session on start; `AT+JOIN=1` forces OTAA rejoin
