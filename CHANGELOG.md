# Changelog

All notable changes to the RAK3162 Zephyr BSP are documented in this file.

Format based on [Keep a Changelog](https://keepachangelog.com/).
Versioning follows [Semantic Versioning](https://semver.org/).

## [Unreleased]

## [1.0.0] - 2026-07-28

First public release of the RAK3162 Zephyr BSP.

Based on **Zephyr v4.3.0**. Intended for practical evaluation and application
development (OTAA Class A + LoRa P2P). Not a full RUI3 firmware replacement.

### Added

- Board support for **RAK3162** (`rak3162/nrf54l15/cpuapp`) with onboard SX1262
  (`semtech,sx1262`, alias `lora0`; no shield required)
- Sample firmware `samples/hw_test`: AT console over UART
- LoRaWAN (Zephyr `CONFIG_LORAWAN` / loramac-node): OTAA Class A, `AT+NWM` /
  `AT+BAND` / `AT+JOIN` / `AT+SEND` / `AT+RECV`, downlink `+EVT:RX_1:...`
- LoRa P2P (Zephyr `CONFIG_LORA`): `AT+P2P` / `AT+PRECV` / `AT+PSEND` / `AT+CW`
- Practical AT command subset (RUI3-inspired); see `samples/hw_test/doc/AT_COMMANDS.md`
- **Mode 1**: dedicated west workspace via `west.yml` + `scripts/bootstrap.sh`
  (venv, Zephyr, SDK, `env.sh`)
- **Mode 2**: use as external Zephyr module via `ZEPHYR_EXTRA_MODULES`
  (optional in-tree copy: `scripts/install_into_zephyr.sh`)
- Minimal module set in `west.yml`: `cmsis`, `cmsis_6`, `hal_nordic`, `loramac-node`
- Hardware pin notes under `doc/` and `boards/rak3162/doc/`

[Unreleased]: https://github.com/flwb-li/rak3162-zephyr-bsp/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/flwb-li/rak3162-zephyr-bsp/releases/tag/v1.0.0
