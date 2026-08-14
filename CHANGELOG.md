# Changelog

All notable changes to the RAK3162 Zephyr BSP are documented in this file.

Format based on [Keep a Changelog](https://keepachangelog.com/).
Versioning follows [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- In-tree `docker/` (Zephyr SDK 0.17.4 image); compose mounts the west workspace parent

### Changed

- Vendored `rak-fw` at `modules/rak-fw` (pinned to BSP revision; sample CMake injects it)
- `.gitignore` keeps `modules/rak-fw` while ignoring other `modules/*`

- Autojoin + periodic uplink moved to `samples/at_firmware/src/main.c`
- Uplink reschedules after send completion / `-EAGAIN` (duty-cycle aware)
- OTAA defaults live in `main.c` (`APP_OTAA_*_HEX`); AT+DEVEUI/APPEUI/APPKEY still override NVS
- Docs: Docker-first README, `LOW_POWER.md`
- Merged AT + LoRaWAN/P2P into `rak-fw` (replaces standalone `rak-at`)
- `rak3162_runtime` is board adapters only (no `src/lora`)
- Renamed product sample `samples/hw_test` → `samples/at_firmware`
- Renamed runtime APIs from `hw_*` to `rak3162_*`
- Settings NVS root default `hwtest` → `rak3162` (existing devices need re-provision)

### Removed

- Framework-level autosend / autojoin APIs (product policy is application-owned)

## [1.0.0] - 2026-07-28

First public release of the RAK3162 Zephyr BSP.

Based on **Zephyr v4.3.0**. Intended for practical evaluation and application
development (OTAA Class A + LoRa P2P). Not a full RUI3 firmware replacement.

### Added

- Board support for **RAK3162** (`rak3162/nrf54l15/cpuapp`) with onboard SX1262
  (`semtech,sx1262`, alias `lora0`; no shield required)
- Sample/firmware app `samples/at_firmware`: UART AT console for LoRaWAN/P2P
- LoRaWAN (Zephyr `CONFIG_LORAWAN` / loramac-node): OTAA Class A, `AT+NWM` /
  `AT+BAND` / `AT+JOIN` / `AT+SEND` / `AT+RECV`, downlink `+EVT:RX_1:...`
- LoRa P2P (Zephyr `CONFIG_LORA`): `AT+P2P` / `AT+PRECV` / `AT+PSEND` / `AT+CW`
- Practical AT command subset (RUI3-inspired); see `samples/at_firmware/doc/AT_COMMANDS.md`
- **Mode 1**: dedicated west workspace via `west.yml` + `scripts/bootstrap.sh`
  (venv, Zephyr, SDK, `env.sh`)
- **Mode 2**: use as external Zephyr module via `ZEPHYR_EXTRA_MODULES`
- Minimal module set in `west.yml`: `cmsis`, `cmsis_6`, `hal_nordic`, `loramac-node`
- Hardware pin notes under `boards/rak3162/doc/`
