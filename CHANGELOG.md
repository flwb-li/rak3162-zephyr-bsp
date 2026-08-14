# Changelog

All notable changes to the RAK3162 Zephyr BSP are documented in this file.

Format based on [Keep a Changelog](https://keepachangelog.com/).
Versioning follows [Semantic Versioning](https://semver.org/).

## [1.0.0] - 2026-08-14

First public release of the RAK3162 Zephyr BSP.

Based on **Zephyr v4.3.0**. Intended for practical evaluation and application
development (OTAA Class A + LoRa P2P). Not a full RUI3 firmware replacement.

### Added

- Board support for **RAK3162** (`rak3162/nrf54l15/cpuapp`) with onboard SX1262
  (`semtech,sx1262`, alias `lora0`; no shield required)
- Product AT firmware `samples/at_firmware`: UART AT console, auto-join,
  `AT+SENDINT` counter uplinks, System ON idle (`AT+SLEEP` = System OFF)
- Vendored framework `modules/rak-fw` (AT + LoRaWAN/P2P; sample CMake injects it)
- Board adapters `lib/rak3162_runtime` (NVS, PM, LED, Sense, radio bind)
- In-tree `docker/` (Zephyr SDK 0.17.4); compose mounts the west workspace parent
- LoRaWAN (Zephyr `CONFIG_LORAWAN` / loramac-node): OTAA Class A, `AT+NWM` /
  `AT+BAND` / `AT+JOIN` / `AT+SEND` / `AT+RECV`, downlink `+EVT:RX_1:...`
- LoRa P2P (Zephyr `CONFIG_LORA`): `AT+P2P` / `AT+PRECV` / `AT+PSEND` / `AT+CW`
- Practical AT command subset (RUI3-inspired); see `samples/at_firmware/doc/AT_COMMANDS.md`
- Docs: Docker-first README (incl. Windows / WSL2 notes), `AT_COMMANDS.md`,
  `LOW_POWER.md`
- West workspace via `west.yml` (minimal modules: `cmsis`, `cmsis_6`,
  `hal_nordic`, `loramac-node`); optional deprecated `scripts/bootstrap.sh`
- Hardware pin notes under `boards/rak3162/doc/`

### Changed

- Product policy (autojoin / SENDINT / OTAA seed) lives in `samples/at_firmware/src/main.c`
- Uplink reschedules after send completion / `-EAGAIN` (duty-cycle aware)
- OTAA defaults in `main.c` (`APP_OTAA_*_HEX`); AT+DEVEUI/APPEUI/APPKEY still override NVS
- Merged AT + LoRaWAN/P2P into `rak-fw` (replaces standalone `rak-at`)
- `rak3162_runtime` is board adapters only (no `src/lora`)
- Renamed product sample `samples/hw_test` → `samples/at_firmware`
- Renamed runtime APIs from `hw_*` to `rak3162_*`
- Settings NVS root default `hwtest` → `rak3162` (existing devices need re-provision)
- `.gitignore` keeps `modules/rak-fw` while ignoring other `modules/*`

### Removed

- Framework-level autosend / autojoin APIs (product policy is application-owned)
- Sample `samples/hw_test` (replaced by `samples/at_firmware`)
